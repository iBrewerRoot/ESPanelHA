/**
 * TCA9554 / TCA9554PWR 8-bit I2C I/O expander.
 *
 * On the Waveshare AMOLED boards the panel reset (and often the touch reset)
 * are not wired to ESP32 GPIOs but to expander pins (EXIO). This minimal driver
 * lets the Display/Touch HAL pulse those lines over the shared I2C bus.
 *
 * Only output usage is implemented (all we need here). The expander shares the
 * board I2C bus with the touch controller — initialise Wire once before use.
 */
#ifndef BOARD_IO_TCA9554_H
#define BOARD_IO_TCA9554_H

#include <Arduino.h>
#include <Wire.h>

namespace hal {

class Tca9554 {
public:
    explicit Tca9554(uint8_t addr, TwoWire &wire = Wire) : addr_(addr), wire_(wire) {}

    /** Probe the device. Returns false if it does not ACK on the bus. */
    bool begin();

    /** Configure the given pins (bit mask) as outputs; others stay inputs. */
    void setOutputs(uint8_t outputMask);

    /** Drive one pin (0..7) high/low. Caches the output register. */
    void write(uint8_t pin, bool level);

private:
    bool writeReg(uint8_t reg, uint8_t value);

    uint8_t addr_;
    TwoWire &wire_;
    uint8_t outputCache_ = 0x00;  // mirror of the output port register
    uint8_t configCache_ = 0xFF;  // 1 = input (power-on default)
};

} // namespace hal

#endif /* BOARD_IO_TCA9554_H */
