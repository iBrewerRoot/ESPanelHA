#include "Imu.h"
#include "BoardConfig.h"
#include "io/I2CBus.h"

#include <Arduino.h>
#include <Wire.h>

namespace hal {

#if defined(BOARD_HAS_IMU) && defined(IMU_DRIVER_QMI8658)

namespace {

// QMI8658 register map (subset).
constexpr uint8_t REG_WHO_AM_I = 0x00;  // == 0x05
constexpr uint8_t REG_CTRL1    = 0x02;  // serial interface / address auto-increment
constexpr uint8_t REG_CTRL2    = 0x03;  // accelerometer ODR + full scale
constexpr uint8_t REG_CTRL7    = 0x08;  // sensor enable
constexpr uint8_t REG_AX_L     = 0x35;  // accel X..Z, 6 bytes little-endian
constexpr float   kAccelScale  = 1.0f / 16384.0f;  // ±2g full scale

bool available = false;

bool writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
    Wire.beginTransmission(IMU_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)IMU_I2C_ADDR, (int)len) != (int)len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

} // namespace

bool imuBegin() {
    boardI2CBegin();  // shared bus, may already be up
    uint8_t who = 0;
    if (!readRegs(REG_WHO_AM_I, &who, 1) || who != 0x05) {
        Serial.printf("[imu] QMI8658 not found (who=0x%02X)\n", who);
        available = false;
        return false;
    }
    writeReg(REG_CTRL1, 0x40);  // enable register address auto-increment
    writeReg(REG_CTRL2, 0x03);  // accel ±2g, low ODR (orientation only)
    writeReg(REG_CTRL7, 0x01);  // enable accelerometer (gyro off)
    available = true;
    Serial.println("[imu] QMI8658 ready");
    return true;
}

bool imuAvailable() { return available; }

bool imuRead(float &ax, float &ay, float &az) {
    if (!available) return false;
    uint8_t b[6];
    if (!readRegs(REG_AX_L, b, 6)) return false;
    const int16_t rx = (int16_t)((b[1] << 8) | b[0]);
    const int16_t ry = (int16_t)((b[3] << 8) | b[2]);
    const int16_t rz = (int16_t)((b[5] << 8) | b[4]);
    ax = rx * kAccelScale;
    ay = ry * kAccelScale;
    az = rz * kAccelScale;
    return true;
}

#else  // no IMU on this board

bool imuBegin() { return false; }
bool imuAvailable() { return false; }
bool imuRead(float &, float &, float &) { return false; }

#endif

} // namespace hal
