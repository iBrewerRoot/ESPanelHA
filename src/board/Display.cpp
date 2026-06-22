#include "Display.h"
#include "BoardConfig.h"
#include "io/I2CBus.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#if defined(BOARD_HAS_IO_EXPANDER)
#include "io/Tca9554.h"
#endif

namespace hal {

namespace {

/* Concrete panel type for the active board, so AMOLED-only calls like
 * setBrightness() are available (they aren't on the Arduino_GFX base). */
#if defined(DISPLAY_DRIVER_CO5300)
using PanelType = Arduino_CO5300;
#elif defined(DISPLAY_DRIVER_SH8601)
using PanelType = Arduino_SH8601;
#else
#error "No supported display driver defined for this board profile."
#endif

PanelType *panel = nullptr;  // typed pointer (brightness, etc.)
Arduino_GFX *gfx = nullptr;  // base pointer (drawing)

lv_disp_t *disp = nullptr;       // registered LVGL display (for live res updates)
lv_disp_drv_t drv;               // kept around so rotation can update hor/ver_res
uint8_t currentRotation = 0;     // Arduino_GFX rotation index (0..3)

/* Partial render buffer: SCREEN_WIDTH x BUFFER_LINES pixels, double-buffered.
 * Allocated from PSRAM when available, otherwise from internal RAM. */
constexpr int kBufferLines = 40;
lv_disp_draw_buf_t drawBuf;
lv_color_t *buf1 = nullptr;
lv_color_t *buf2 = nullptr;
lv_color_t *rotBuf = nullptr;  // scratch for software rotation (landscape/180°)

/* Push an LVGL rendered area to the panel. The CO5300/SH8601 controllers don't
 * support hardware rotation (the driver only flips axes, which mangles
 * landscape), so the panel stays at its native portrait orientation and we
 * rotate the partial buffer here into native coordinates. LVGL stores native
 * RGB565 (LV_COLOR_16_SWAP=0), exactly what draw16bitRGBBitmap expects. */
void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    const int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
    const int aw = x2 - x1 + 1, ah = y2 - y1 + 1;

    if (currentRotation == 0 || !rotBuf) {
        gfx->draw16bitRGBBitmap(x1, y1, reinterpret_cast<uint16_t *>(pixels), aw, ah);
        lv_disp_flush_ready(drv);
        return;
    }

    constexpr int NW = SCREEN_WIDTH, NH = SCREEN_HEIGHT;  // native portrait size
    lv_color_t *dst = rotBuf;
    int nx0 = 0, ny0 = 0, nw = aw, nh = ah;

    // Map each logical pixel to its native panel position.
    switch (currentRotation) {
        case 1:  // landscape
            nw = ah; nh = aw; nx0 = NW - 1 - y2; ny0 = x1;
            for (int ly = y1; ly <= y2; ly++)
                for (int lx = x1; lx <= x2; lx++)
                    dst[(lx - x1) * nw + (y2 - ly)] = pixels[(ly - y1) * aw + (lx - x1)];
            break;
        case 2:  // 180°
            nw = aw; nh = ah; nx0 = NW - 1 - x2; ny0 = NH - 1 - y2;
            for (int ly = y1; ly <= y2; ly++)
                for (int lx = x1; lx <= x2; lx++)
                    dst[(y2 - ly) * nw + (x2 - lx)] = pixels[(ly - y1) * aw + (lx - x1)];
            break;
        default:  // 3: landscape (opposite of case 1)
            nw = ah; nh = aw; nx0 = y1; ny0 = NH - 1 - x2;
            for (int ly = y1; ly <= y2; ly++)
                for (int lx = x1; lx <= x2; lx++)
                    dst[(x2 - lx) * nw + (ly - y1)] = pixels[(ly - y1) * aw + (lx - x1)];
            break;
    }
    gfx->draw16bitRGBBitmap(nx0, ny0, reinterpret_cast<uint16_t *>(dst), nw, nh);
    lv_disp_flush_ready(drv);
}

lv_color_t *allocBuffer(size_t pixelCount) {
#if defined(BOARD_HAS_PSRAM)
    auto *p = static_cast<lv_color_t *>(
        heap_caps_malloc(pixelCount * sizeof(lv_color_t), MALLOC_CAP_SPIRAM));
    if (p) return p;
#endif
    return static_cast<lv_color_t *>(
        heap_caps_malloc(pixelCount * sizeof(lv_color_t), MALLOC_CAP_DMA));
}

/* Hardware-reset the panel (and touch) before init. On these boards the reset
 * lines hang off a TCA9554 expander rather than GPIOs, so the pulse is issued
 * over I2C: assert the EXIO reset lines low, then release them high. */
void resetPanel() {
#if defined(BOARD_HAS_IO_EXPANDER)
    boardI2CBegin();
    static Tca9554 expander(IO_EXPANDER_ADDR);
    if (!expander.begin()) {
        Serial.println("[display] TCA9554 I/O expander not found on I2C");
        return;
    }

    const uint8_t pins[] = EXIO_RESET_PINS;
    uint8_t mask = 0;
    for (uint8_t p : pins) mask |= (1u << p);
    expander.setOutputs(mask);

    for (uint8_t p : pins) expander.write(p, false);  // assert reset
    delay(20);
    for (uint8_t p : pins) expander.write(p, true);   // release reset
    delay(50);
#endif
}

PanelType *makePanel() {
    auto *bus = new Arduino_ESP32QSPI(
        LCD_QSPI_CS, LCD_QSPI_SCK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
    return new PanelType(bus, LCD_RST, 0 /*rotation*/, SCREEN_WIDTH, SCREEN_HEIGHT,
                         LCD_COL_OFFSET, LCD_ROW_OFFSET, 0, 0);
}

// These QSPI AMOLED controllers address pixels in pairs, so a flush whose column
// start or width is odd shears the image. In landscape the logical Y axis maps to
// the native column axis, so vertical scrolling hits odd columns and tears. Snap
// every flush area to even start + even size so all 4 rotations stay aligned.
void rounderCb(lv_disp_drv_t *, lv_area_t *a) {
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

} // namespace

void displayInit() {
    resetPanel();

    panel = makePanel();
    gfx = panel;
    if (!panel->begin()) {
        Serial.println("[display] panel begin() failed");
    }
    panel->fillScreen(0x0000 /*black*/);
    panel->setBrightness(255);  // AMOLED: panel starts dark until brightness set

    const size_t bufPixels = static_cast<size_t>(SCREEN_WIDTH) * kBufferLines;
    // rounderCb can grow a chunk by up to 2 rows; over-allocate by 2 max-width
    // rows so that growth never overruns the buffers (LVGL is told the nominal
    // size, so its chunking is unchanged).
    const size_t maxW = SCREEN_WIDTH > SCREEN_HEIGHT ? SCREEN_WIDTH : SCREEN_HEIGHT;
    const size_t allocPixels = bufPixels + 2 * maxW;
    buf1 = allocBuffer(allocPixels);
    buf2 = allocBuffer(allocPixels);
    rotBuf = allocBuffer(allocPixels);  // software-rotation scratch (landscape/180°)
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

    lv_disp_drv_init(&drv);
    drv.hor_res = SCREEN_WIDTH;
    drv.ver_res = SCREEN_HEIGHT;
    drv.flush_cb = flushCb;
    drv.rounder_cb = rounderCb;  // even-align flushes (pair-addressed AMOLED)
    drv.draw_buf = &drawBuf;
    disp = lv_disp_drv_register(&drv);
}

void displaySetRotation(uint8_t rotation) {
    rotation &= 0x03;
    currentRotation = rotation;
    // The panel stays native (rotation 0); flushCb rotates pixels in software.
    // Only the LVGL logical resolution swaps for the landscape rotations (1/3).
    const bool landscape = rotation & 1;
    drv.hor_res = landscape ? SCREEN_HEIGHT : SCREEN_WIDTH;
    drv.ver_res = landscape ? SCREEN_WIDTH : SCREEN_HEIGHT;
    if (disp) lv_disp_drv_update(disp, &drv);
}

uint8_t displayRotation() { return currentRotation; }

int displayWidth() { return (currentRotation & 1) ? SCREEN_HEIGHT : SCREEN_WIDTH; }
int displayHeight() { return (currentRotation & 1) ? SCREEN_WIDTH : SCREEN_HEIGHT; }

} // namespace hal
