# Third-Party Notices

The source code of this project is licensed under the **MIT License** (see
[`LICENSE`](LICENSE)). It bundles and/or builds on the third-party components
listed below, each governed by its own license. This file is provided for
attribution and license compliance.

## Assets bundled in this repository

These generated/embedded assets are derived from third-party fonts and icons and
are redistributed here under their respective licenses.

### Montserrat (typeface)
- **Files:** `scripts/mdi-font/Montserrat-Medium.ttf`, `src/ui/fonts/montserrat_fr_*.c`
  (LVGL font subsets generated from the TTF).
- **License:** SIL Open Font License, Version 1.1 — see [`OFL.txt`](OFL.txt).
- **Copyright:** © 2011 The Montserrat Project Authors —
  https://github.com/JulietaUla/Montserrat
- The OFL permits embedding and modification; the font may not be sold on its own,
  and derivatives must remain under the OFL.

### Material Design Icons (icon set)
- **Files:** `src/ui/fonts/mdi_font.c`, `src/ui/mdi_icons.{h,cpp}`,
  `web/src/mdi-sprite.svg` — generated from the `@mdi/font` and `@mdi/svg`
  packages (see `scripts/mdi-font`).
- **License:** Apache-2.0 (Pictogrammers Free License).
- **Source:** https://pictogrammers.com/library/mdi/

## Build & runtime dependencies

Pulled in at build time by PlatformIO (`lib_deps`) / npm — **not** redistributed
inside this repository, but linked into the firmware or used by the toolchain.

| Component | Author | License |
|---|---|---|
| [LVGL](https://lvgl.io/) | LVGL Kft. | MIT |
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | moononournation | See upstream repository |
| [ArduinoJson](https://arduinojson.org/) | Benoît Blanchon | MIT |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | tzapu | MIT |
| [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) | Markus Sattler (Links2004) | LGPL-2.1 |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | ESP32Async | LGPL-3.0 |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | ESP32Async | LGPL-3.0 |
| [lv_font_conv](https://github.com/lvgl/lv_font_conv) (dev tool) | LVGL | MIT |

### Note on the LGPL libraries

`arduinoWebSockets`, `ESPAsyncWebServer` and `AsyncTCP` are licensed under the
LGPL. This does **not** relicense this project's own code, which stays under the
MIT License. Because this is an open-source project distributed with full source
(and the libraries are fetched, unmodified, from their upstream repositories),
the LGPL terms are satisfied. If you distribute a **binary-only** build, make
sure you still meet the LGPL obligations (provide the library sources and a way
to relink them).
