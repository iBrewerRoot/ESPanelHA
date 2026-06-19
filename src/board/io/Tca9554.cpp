#include "Tca9554.h"

namespace hal {

namespace {
// TCA9554 register map.
constexpr uint8_t kRegOutput = 0x01;  // output port
constexpr uint8_t kRegConfig = 0x03;  // 1 = input, 0 = output
} // namespace

bool Tca9554::begin() {
    wire_.beginTransmission(addr_);
    return wire_.endTransmission() == 0;
}

bool Tca9554::writeReg(uint8_t reg, uint8_t value) {
    wire_.beginTransmission(addr_);
    wire_.write(reg);
    wire_.write(value);
    return wire_.endTransmission() == 0;
}

void Tca9554::setOutputs(uint8_t outputMask) {
    configCache_ &= ~outputMask;  // selected bits become outputs (0)
    writeReg(kRegConfig, configCache_);
}

void Tca9554::write(uint8_t pin, bool level) {
    if (pin > 7) return;
    if (level) outputCache_ |= (1u << pin);
    else outputCache_ &= ~(1u << pin);
    writeReg(kRegOutput, outputCache_);
}

} // namespace hal
