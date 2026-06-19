/**
 * Shared board I2C bus. The touch controller and the TCA9554 I/O expander sit
 * on the same bus, so it must be brought up exactly once before either is used.
 */
#ifndef BOARD_IO_I2C_BUS_H
#define BOARD_IO_I2C_BUS_H

namespace hal {

/** Initialise Wire on the board I2C pins. Idempotent (safe to call repeatedly). */
void boardI2CBegin();

/** Probe every I2C address and print responders to Serial (bring-up helper). */
void boardI2CScan();

} // namespace hal

#endif /* BOARD_IO_I2C_BUS_H */
