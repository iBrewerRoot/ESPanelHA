/**
 * IMU HAL (optional).
 *
 * Exposes the board accelerometer for auto-rotation. Board-gated: on boards
 * without an IMU (no BOARD_HAS_IMU) every call is a no-op returning false, so
 * the rest of the app can call these unconditionally.
 */
#ifndef HAL_IMU_H
#define HAL_IMU_H

namespace hal {

/** Probe + configure the IMU. Returns true if present and initialized. */
bool imuBegin();

/** True once imuBegin() succeeded. */
bool imuAvailable();

/** Read the accelerometer in units of g (chip axes). False if absent / I2C error. */
bool imuRead(float &ax, float &ay, float &az);

} // namespace hal

#endif /* HAL_IMU_H */
