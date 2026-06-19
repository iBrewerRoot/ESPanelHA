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

/* Partial render buffer: SCREEN_WIDTH x BUFFER_LINES pixels, double-buffered.
 * Allocated from PSRAM when available, otherwise from internal RAM. */
constexpr int kBufferLines = 40;
lv_disp_draw_buf_t drawBuf;
lv_color_t *buf1 = nullptr;
lv_color_t *buf2 = nullptr;

/* Push an LVGL rendered area to the panel. LVGL stores native RGB565
 * (LV_COLOR_16_SWAP=0), which is exactly what draw16bitRGBBitmap expects. */
void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    const int32_t w = area->x2 - area->x1 + 1;
    const int32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t *>(pixels), w, h);
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
    buf1 = allocBuffer(bufPixels);
    buf2 = allocBuffer(bufPixels);
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, bufPixels);

    static lv_disp_drv_t drv;
    lv_disp_drv_init(&drv);
    drv.hor_res = SCREEN_WIDTH;
    drv.ver_res = SCREEN_HEIGHT;
    drv.flush_cb = flushCb;
    drv.draw_buf = &drawBuf;
    lv_disp_drv_register(&drv);
}

int displayWidth() { return SCREEN_WIDTH; }
int displayHeight() { return SCREEN_HEIGHT; }

} // namespace hal
