/**
 * LVGL 8.x configuration.
 *
 * Only the options that differ from the LVGL defaults are set here; every
 * other option falls back to its built-in default (lv_conf_internal.h guards
 * each option with #ifndef). Selected via -D LV_CONF_INCLUDE_SIMPLE.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 16-bit color to match the AMOLED panels. We flush via Arduino_GFX's
 * draw16bitRGBBitmap(), which expects native RGB565 values (same as the
 * Waveshare CO5300 demo image arrays), so NO byte swap here. Swapping would
 * corrupt asymmetric colors while leaving pure black/white intact. */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* LVGL heap. On PSRAM boards this can grow; kept modest so the C6 (no PSRAM)
 * also fits. Buffers used for rendering are allocated separately in Display. */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (48U * 1024U)

/* Tick is fed from millis() in main loop via lv_tick_inc through a callback. */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

/* Default refresh / input read periods (ms). */
#define LV_DISP_DEF_REFR_PERIOD 16
#define LV_INDEV_DEF_READ_PERIOD 16

/* The UI renders text with our accented Montserrat subsets (montserrat_fr_*,
 * see src/ui/text_fonts.h) so French names display correctly. Only the built-in
 * 24px stays enabled as LV_FONT_DEFAULT (fallback for objects with no explicit
 * font); the other built-in sizes are off to save flash. */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_DEFAULT &lv_font_montserrat_24

/* Widgets needed by the dashboard / setup screens. */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_SWITCH 1
#define LV_USE_SLIDER 1
#define LV_USE_LABEL 1
#define LV_USE_BTN 1
#define LV_USE_LIST 1
#define LV_USE_TILEVIEW 1   /* swipeable dashboard pages */

/* Logging — set to LV_LOG_LEVEL_WARN while bringing up new boards. */
#define LV_USE_LOG 0

#endif /* LV_CONF_H */
