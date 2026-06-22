#include "Touch.h"
#include "BoardConfig.h"
#include "io/I2CBus.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

namespace hal {

namespace {

ITouch *touch = nullptr;
uint8_t touchRotation = 0;  // mirrors the display rotation (0..3)

// Map a raw touch point (reported in the panel's native portrait orientation) to
// the current logical orientation. This is the exact inverse of the software
// rotation applied in Display::flushCb, so touch and pixels stay aligned.
void applyRotation(int16_t &x, int16_t &y) {
    const int16_t rx = x, ry = y;
    switch (touchRotation) {
        case 1:  x = ry;                       y = (SCREEN_WIDTH - 1) - rx;  break;
        case 2:  x = (SCREEN_WIDTH - 1) - rx;  y = (SCREEN_HEIGHT - 1) - ry; break;
        case 3:  x = (SCREEN_HEIGHT - 1) - ry; y = rx;                       break;
        default: /* 0: native portrait */                                    break;
    }
}

#if defined(TOUCH_DRIVER_CAP)
/**
 * Generic capacitive touch controller (I2C) for the FocalTech-style register
 * layout shared by CST816 (addr 0x15) and FT3168 (addr 0x38): reg 0x02 holds
 * the touch count, 0x03..0x06 the first point's X/Y high/low bytes.
 */
class TouchCapacitive : public ITouch {
public:
    bool begin() override {
        // Shared bus + touch reset are already up (Display ran first).
        boardI2CBegin();
        return true;
    }

    bool read(int16_t &x, int16_t &y) override {
        // Register 0x02 = number of touch points; 0x03.. = first point data.
        Wire.beginTransmission(TOUCH_I2C_ADDR);
        Wire.write(0x02);
        if (Wire.endTransmission(false) != 0) return false;
        if (Wire.requestFrom((int)TOUCH_I2C_ADDR, 5) != 5) return false;

        const uint8_t points = Wire.read() & 0x0F;
        const uint8_t xh = Wire.read();
        const uint8_t xl = Wire.read();
        const uint8_t yh = Wire.read();
        const uint8_t yl = Wire.read();
        if (points == 0) return false;

        x = ((xh & 0x0F) << 8) | xl;
        y = ((yh & 0x0F) << 8) | yl;
        return true;
    }
};
#endif

ITouch *makeTouch() {
#if defined(TOUCH_DRIVER_CAP)
    return new TouchCapacitive();
#else
#error "No supported touch driver defined for this board profile."
#endif
}

void readCb(lv_indev_drv_t *, lv_indev_data_t *data) {
    int16_t x = 0, y = 0;
    if (touch && touch->read(x, y)) {
        applyRotation(x, y);
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

} // namespace

void touchSetRotation(uint8_t rotation) { touchRotation = rotation & 0x03; }

void touchInit() {
    touch = makeTouch();
    if (!touch->begin()) {
        Serial.println("[touch] begin() failed");
    }

    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_POINTER;
    drv.read_cb = readCb;
    lv_indev_drv_register(&drv);
}

} // namespace hal
