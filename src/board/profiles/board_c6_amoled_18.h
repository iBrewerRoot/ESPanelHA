/**
 * Profile: Waveshare ESP32-C6 Touch AMOLED 1.8"
 *   - 368 x 448, SH8601 AMOLED controller over QSPI
 *   - FT3168 capacitive touch over I2C (addr 0x38)
 *   - LCD/touch reset driven by a TCA9554 I/O expander (addr 0x20, EXIO 0/1/2)
 *   - ESP32-C6: single-core RISC-V, NO PSRAM
 *
 * Pins verified against the official Waveshare demo (pin_config.h) and the
 * GFX/touch example sketches:
 *   github.com/waveshareteam/ESP32-C6-Touch-AMOLED-1.8 (Arduino-v3.3.5)
 *
 * Note: this C6 board uses different controllers AND a different pinout than the
 * S3 1.8" board (SH8601/FT3168 vs CO5300/CST816) — the HAL handles both.
 */
#ifndef BOARD_C6_AMOLED_18_H
#define BOARD_C6_AMOLED_18_H

#define BOARD_NAME "ESP32-C6 AMOLED 1.8\""

#define SCREEN_WIDTH 368
#define SCREEN_HEIGHT 448

/* Display: SH8601, no panel offset. */
#define DISPLAY_DRIVER_SH8601 1
#define LCD_COL_OFFSET 0
#define LCD_ROW_OFFSET 0

/* QSPI bus to the panel. */
#define LCD_QSPI_CS 5
#define LCD_QSPI_SCK 0
#define LCD_QSPI_D0 1
#define LCD_QSPI_D1 2
#define LCD_QSPI_D2 3
#define LCD_QSPI_D3 4
#define LCD_RST -1 /* reset is on the I/O expander */

/* Shared I2C bus: touch + expander (+ AXP2101 PMIC, ES8311 codec). */
#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 7

/* Touch: FT3168 (FocalTech-compatible register layout). */
#define TOUCH_DRIVER_CAP 1
#define TOUCH_I2C_INT 15
#define TOUCH_I2C_ADDR 0x38

/* TCA9554 I/O expander: EXIO 0,1,2 are the reset lines. */
#define BOARD_HAS_IO_EXPANDER 1
#define IO_EXPANDER_ADDR 0x20
#define EXIO_RESET_PINS {0, 1, 2}

/* AXP2101 PMIC on the shared I2C bus (0x34) — battery gauge + USB/VBUS sensing.
 * Presence/registers to be confirmed on hardware at bring-up. */
#define BOARD_HAS_PMIC_AXP2101 1
#define PMIC_I2C_ADDR 0x34

#endif /* BOARD_C6_AMOLED_18_H */
