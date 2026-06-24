/**
 * Power / battery HAL.
 *
 * Thin reader for the board PMIC (AXP2101 on the Waveshare AMOLED boards), used
 * to tell USB from battery operation and to read the battery gauge. Boards
 * without a PMIC compile to no-ops, so the app code stays board-agnostic.
 */
#ifndef HAL_POWER_H
#define HAL_POWER_H

#include <stdint.h>

namespace hal {

/** Probe the PMIC on the shared I2C bus. Returns false if none is present. */
bool powerBegin();

/** True if a PMIC was found at boot. */
bool powerAvailable();

/** True if a battery is connected to the PMIC. */
bool batteryPresent();

/** True when running on battery (no external USB/VBUS power). */
bool onBattery();

/** True while the battery is charging. */
bool batteryCharging();

/** Battery charge level, 0..100 (-1 if unknown / no PMIC / no battery). */
int batteryPercent();

} // namespace hal

#endif /* HAL_POWER_H */
