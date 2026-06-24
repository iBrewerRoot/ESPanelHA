/**
 * Profile: Waveshare ESP32-S3 Touch AMOLED 1.8"  (current "v2" revision)
 *   - 368 x 448, CO5300 AMOLED controller over QSPI (column offset 16)
 *   - CST816 capacitive touch over I2C (addr 0x15)
 *   - LCD/touch reset driven by a TCA9554 I/O expander (addr 0x20, EXIO 0/1/2)
 *
 * Pins verified against the official Waveshare demo (pin_config.h) and the
 * 04_GFX example sketch:
 *   github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8 (Arduino-v3.3.5-v2)
 *
 * Note: an earlier "v1" revision of this board uses SH8601 + FT3168 (touch at
 * 0x38). The boot-time I2C scan disambiguates: 0x15 => CST816 (this profile),
 * 0x38 => FT3168 (switch DISPLAY_DRIVER_/TOUCH_I2C_ADDR accordingly).
 */
#ifndef BOARD_S3_AMOLED_18_H
#define BOARD_S3_AMOLED_18_H

#define BOARD_NAME "ESP32-S3 AMOLED 1.8\""

#define SCREEN_WIDTH 368
#define SCREEN_HEIGHT 448

/* Display: CO5300 with a 16-px column offset (panel is centered in controller RAM). */
#define DISPLAY_DRIVER_CO5300 1
#define LCD_COL_OFFSET 16
#define LCD_ROW_OFFSET 0

/* QSPI bus to the panel. */
#define LCD_QSPI_CS 12
#define LCD_QSPI_SCK 11
#define LCD_QSPI_D0 4
#define LCD_QSPI_D1 5
#define LCD_QSPI_D2 6
#define LCD_QSPI_D3 7
#define LCD_RST -1 /* reset is on the I/O expander, not a GPIO */

/* Shared I2C bus: touch + expander (+ AXP2101 PMIC, ES8311 codec). */
#define BOARD_I2C_SDA 15
#define BOARD_I2C_SCL 14

/* Touch: CST816 (FocalTech-compatible register layout). */
#define TOUCH_DRIVER_CAP 1
#define TOUCH_I2C_INT 21
#define TOUCH_I2C_ADDR 0x15

/* Deep-sleep touch wake: the touch INT line (GPIO21) is RTC-capable on the S3,
 * so ext0 can wake the chip on a touch. (Not defined on boards whose INT pin is
 * outside the RTC/LP domain — deep sleep then stays disabled there.) */
#define BOARD_DEEP_SLEEP_WAKE_GPIO TOUCH_I2C_INT

/* TCA9554 I/O expander: EXIO 0,1,2 are the reset lines (assert low, release high). */
#define BOARD_HAS_IO_EXPANDER 1
#define IO_EXPANDER_ADDR 0x20
#define EXIO_RESET_PINS {0, 1, 2}

/* QMI8658 6-axis IMU on the shared I2C bus — drives optional auto-rotation.
 * Address 0x6B (SA0 high); confirm with the boot I2C scan (WHO_AM_I 0x00 == 0x05). */
#define BOARD_HAS_IMU 1
#define IMU_DRIVER_QMI8658 1
#define IMU_I2C_ADDR 0x6B

/* AXP2101 PMIC on the shared I2C bus (0x34) — battery gauge + USB/VBUS sensing.
 * Registers verified on hardware at bring-up (see board/Power.cpp). */
#define BOARD_HAS_PMIC_AXP2101 1
#define PMIC_I2C_ADDR 0x34

/* This board has Octal PSRAM (set via BOARD_HAS_PSRAM in build_flags). */

#endif /* BOARD_S3_AMOLED_18_H */
