#include "Power.h"
#include "BoardConfig.h"
#include "io/I2CBus.h"

#include <Arduino.h>
#include <Wire.h>

namespace hal {

#if defined(BOARD_HAS_PMIC_AXP2101)

namespace {

// AXP2101 register map (subset). Values follow the AXP2101 datasheet / XPowersLib;
// confirm against the Waveshare demo on real hardware before relying on them.
constexpr uint8_t REG_STATUS1 = 0x00;  // bit5 VBUS good, bit3 battery present
constexpr uint8_t REG_STATUS2 = 0x01;  // bits[6:5] charge direction (01 = charging)
constexpr uint8_t REG_BAT_PCT = 0xA4;  // fuel-gauge battery percentage (0..100)

constexpr uint8_t BIT_VBUS_GOOD = 0x20;  // STATUS1 bit5
constexpr uint8_t BIT_BAT_PRESENT = 0x08;  // STATUS1 bit3

bool available = false;

bool readReg(uint8_t reg, uint8_t &out) {
    Wire.beginTransmission(PMIC_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)PMIC_I2C_ADDR, 1) != 1) return false;
    out = Wire.read();
    return true;
}

} // namespace

bool powerBegin() {
    boardI2CBegin();  // shared bus, may already be up
    Wire.beginTransmission(PMIC_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[power] AXP2101 PMIC not found on I2C");
        available = false;
        return false;
    }
    available = true;
    Serial.println("[power] AXP2101 PMIC ready");
    return true;
}

bool powerAvailable() { return available; }

bool batteryPresent() {
    uint8_t s;
    if (!available || !readReg(REG_STATUS1, s)) return false;
    return s & BIT_BAT_PRESENT;
}

bool onBattery() {
    uint8_t s;
    if (!available || !readReg(REG_STATUS1, s)) return false;
    return !(s & BIT_VBUS_GOOD);  // no VBUS -> running on battery
}

bool batteryCharging() {
    uint8_t s;
    if (!available || !readReg(REG_STATUS2, s)) return false;
    return ((s >> 5) & 0x03) == 0x01;  // 01 = charging
}

int batteryPercent() {
    uint8_t p;
    if (!available || !readReg(REG_BAT_PCT, p)) return -1;
    return p > 100 ? 100 : p;
}

#else  // no PMIC on this board

bool powerBegin() { return false; }
bool powerAvailable() { return false; }
bool batteryPresent() { return false; }
bool onBattery() { return false; }
bool batteryCharging() { return false; }
int batteryPercent() { return -1; }

#endif

} // namespace hal
