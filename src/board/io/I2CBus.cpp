#include "I2CBus.h"
#include "../BoardConfig.h"

#include <Arduino.h>
#include <Wire.h>

namespace hal {

namespace {
bool started = false;
}

void boardI2CBegin() {
    if (started) return;
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire.setClock(400000);
    started = true;
}

void boardI2CScan() {
    boardI2CBegin();
    Serial.printf("[i2c] scanning bus (SDA=%d SCL=%d)...\n", BOARD_I2C_SDA, BOARD_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[i2c]   device found at 0x%02X\n", addr);
            found++;
        }
    }
    Serial.printf("[i2c] scan done, %d device(s) found\n", found);
}

} // namespace hal
