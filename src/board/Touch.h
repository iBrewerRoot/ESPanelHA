/**
 * Touch HAL.
 *
 * ITouch abstracts the per-board capacitive controller. touchInit() picks the
 * implementation from the board profile and registers it as an LVGL pointer
 * input device. Adding a new controller = one new ITouch implementation.
 */
#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include <stdint.h>

namespace hal {

class ITouch {
public:
    virtual ~ITouch() = default;
    virtual bool begin() = 0;
    /** Returns true if currently touched; writes coordinates when touched. */
    virtual bool read(int16_t &x, int16_t &y) = 0;
};

/** Build the board's touch driver and bind it to an LVGL indev. Call once. */
void touchInit();

} // namespace hal

#endif /* HAL_TOUCH_H */
