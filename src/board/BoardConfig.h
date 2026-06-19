/**
 * Board selection entry point.
 *
 * Exactly one BOARD_* macro is defined by the active PlatformIO environment
 * (see platformio.ini). This header includes the matching profile, which
 * defines the screen geometry, the display/touch driver kinds and their pins.
 *
 * To add a board: create src/board/profiles/<name>.h, define its BOARD_* macro
 * in a new [env:*], and add one branch below. Nothing else in the codebase
 * needs to change.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#if defined(BOARD_S3_AMOLED_18)
#include "profiles/board_s3_amoled_18.h"
#elif defined(BOARD_C6_AMOLED_18)
#include "profiles/board_c6_amoled_18.h"
#else
#error "No board profile selected. Define a BOARD_* macro in platformio.ini build_flags."
#endif

/* Sanity checks every profile must satisfy. */
#if !defined(SCREEN_WIDTH) || !defined(SCREEN_HEIGHT)
#error "Board profile must define SCREEN_WIDTH and SCREEN_HEIGHT."
#endif
#if !defined(BOARD_NAME)
#error "Board profile must define BOARD_NAME."
#endif

#endif /* BOARD_CONFIG_H */
