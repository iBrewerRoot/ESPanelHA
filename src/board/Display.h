/**
 * Display HAL.
 *
 * Builds the correct Arduino_GFX device for the active board profile and wires
 * it to LVGL (draw buffer + flush callback). The rest of the app only ever
 * talks to LVGL — no board-specific display code leaks upward.
 */
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdint.h>

namespace hal {

/** Initialize the panel and register the LVGL display driver. Call once. */
void displayInit();

/** Apply an Arduino_GFX rotation (0..3) at runtime: rotates the panel and swaps
 *  the LVGL resolution for the landscape rotations (1/3). The UI must be rebuilt
 *  afterwards (and touch re-mapped) to match the new geometry. */
void displaySetRotation(uint8_t rotation);

/** Current rotation index (0..3). */
uint8_t displayRotation();

/** Screen geometry as seen by LVGL (reflects the current rotation). */
int displayWidth();
int displayHeight();

/** Set the active panel brightness as a percentage (1..100). Remembered as the
 *  "awake" level so displayWake() can restore it after a sleep. */
void displaySetBrightness(uint8_t pct);

/** Current active brightness percentage (the level displayWake() restores). */
uint8_t displayBrightness();

/** Blank the panel (brightness 0). On AMOLED this is near-zero power; the active
 *  level is kept so displayWake() restores it. Idempotent. */
void displaySleep();

/** Restore the panel to the active brightness after displaySleep(). Idempotent. */
void displayWake();

/** True while the panel is blanked by displaySleep(). */
bool displayIsAsleep();

} // namespace hal

#endif /* HAL_DISPLAY_H */
