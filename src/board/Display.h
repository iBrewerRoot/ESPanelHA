/**
 * Display HAL.
 *
 * Builds the correct Arduino_GFX device for the active board profile and wires
 * it to LVGL (draw buffer + flush callback). The rest of the app only ever
 * talks to LVGL — no board-specific display code leaks upward.
 */
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

namespace hal {

/** Initialize the panel and register the LVGL display driver. Call once. */
void displayInit();

/** Screen geometry as seen by LVGL (after rotation). */
int displayWidth();
int displayHeight();

} // namespace hal

#endif /* HAL_DISPLAY_H */
