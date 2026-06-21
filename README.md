<div align="center">

<img src="docs/branding/logo-512.png" alt="ESPanelHA logo" width="120" />

# ESPanelHA

### ESP32 AMOLED touch panel for Home Assistant

**Turn a Waveshare AMOLED touch display into a polished, real‑time Home Assistant wall panel.**

WiFi + HA onboarding, automatic entity discovery, a WYSIWYG web editor to design your screens, and tactile control with live state — all from a single multi‑board firmware.

<br/>

![Platform](https://img.shields.io/badge/platform-PlatformIO-FF7F2A?logo=platformio&logoColor=white)
![MCU](https://img.shields.io/badge/MCU-ESP32--S3%20%7C%20ESP32--C6-E7352C?logo=espressif&logoColor=white)
![UI](https://img.shields.io/badge/UI-LVGL%208.x-1B1F23)
![Home Assistant](https://img.shields.io/badge/Home%20Assistant-WebSocket%20API-41BDF5?logo=homeassistant&logoColor=white)
![Status](https://img.shields.io/badge/status-MVP-2EA043)

</div>

<!-- 🎬 GIF at docs/media/hero.gif:
<p align="center"><img src="docs/media/hero.gif" alt="Dashboard in action" width="320" /></p>
-->

> 📸 **Screenshots & demo GIFs coming soon.**

---

## ✨ Features

- ⚡ **Real‑time control** over Home Assistant's **native WebSocket API**. Tap a tile and HA reacts instantly; changes made elsewhere reflect on screen.
- 🔍 **Automatic entity discovery** — lights, switches and sensors show up on their own.
- 🖱️ **WYSIWYG web editor** — design multi‑page screens from your browser; the canvas is a pixel‑faithful, drag‑and‑drop replica of the actual panel.
- 📱 **Swipeable multi‑page dashboard** and a pull‑down **settings menu**.
- 🧩 **Multi‑board HAL** — the same app runs across different Waveshare panels; only a board profile changes.
- 🔐 **`ws://` or `wss://` (TLS)** — insecure TLS mode for trusted LANs, self‑signed HA certs and Nabu Casa.
- 🔄 **Browser OTA updates** from the config portal.
- 🧵 **Cooperative, non‑blocking loop** — runs on single‑core C6 and dual‑core S3 alike.

---

## 🖼️ Screenshots

| Dashboard | Web editor (WYSIWYG) | Settings menu |
|:--:|:--:|:--:|
| _coming soon_ | _coming soon_ | _coming soon_ |

<!--
| ![Dashboard](docs/media/dashboard.png) | ![Web editor](docs/media/editor.png) | ![Settings](docs/media/settings.gif) |
-->

---

## 🧰 Supported hardware

| Board | Resolution | Display | Touch | Status |
|---|---|---|---|---|
| **ESP32‑S3 Touch AMOLED 1.8"** | 368×448 | CO5300 (QSPI) | CST816 @0x15 | ✅ Reference (validated) |
| **ESP32‑C6 Touch AMOLED 1.8"** | 368×448 | SH8601 (QSPI) | FT3168 @0x38 | 🟡 Profile ready (untested) |
| 2.16" / 1.75" / 2.41" | 480×480 / 466×466 / 536×240 | CO5300 / RM690B0 | TBD | 🔭 Planned |

<details>
<summary>Hardware notes (pins, reset, board revisions)</summary>

- Pins, controllers and the I/O‑expander mapping are **verified against Waveshare's official demo code** (`pin_config.h` + GFX examples). The S3 and C6 1.8" boards differ on **both** controllers (display *and* touch) and on pinout — the HAL absorbs this through per‑board profiles.
- Screen/touch **reset is not on a GPIO** but on a **TCA9554** I/O expander (@0x20, EXIO 0/1/2, shared I²C bus). [`Tca9554`](src/board/io/Tca9554.h) issues the reset sequence before panel init ([`Display.cpp`](src/board/Display.cpp) → `resetPanel()`).
- ⚠️ A **v1** revision of the S3 1.8" uses SH8601 + FT3168 (@0x38). The boot‑time **I²C scan** (`-D DEBUG_I2C_SCAN`, on by default for S3) tells them apart: `0x15` ⇒ CST816 (current profile), `0x38` ⇒ FT3168 (switch `DISPLAY_DRIVER_*` and `TOUCH_I2C_ADDR`).

</details>

---

## 🏗️ Architecture

Independent layers — the application never depends on a specific board:

```
ui/    (LVGL)         boot / setup / dashboard / settings screens, responsive
ha/    (HAClient)     native HA WebSocket API + EntityStore (state model)
net/   (WiFi/portal)  WiFiManager + web config portal + NVS/LittleFS storage
board/ (HAL)          BoardConfig + Display (Arduino_GFX) + Touch (ITouch)
core/                 shared config types
```

**Single source of truth:** dashboard geometry and style live in `ui::DashboardSpec` ([UiTheme.h](src/ui/UiTheme.h)). The device renders from it and the config portal serves the same values at `GET /api/device`, so the browser preview can never drift from the real screen.

**Generated assets:** the Material Design **icon font**, the web **SVG icon sprite**, and the accented **Montserrat text fonts** are all produced from `scripts/mdi-font` (`npm run gen`) — see [Regenerating icons & fonts](#-regenerating-icons--fonts).

---

## 🚀 Build & flash

Platform: **[pioarduino](https://github.com/pioarduino/platform-espressif32)** (Arduino‑ESP32 3.x / IDF 5.x, required for the C6).

```bash
# Reference board (S3 1.8")
pio run -e s3_amoled_18
pio run -e s3_amoled_18 -t upload -t monitor

# Other board
pio run -e c6_amoled_18 -t upload
```

> 💡 **Dev shortcut:** create `include/secrets.h` (gitignored, see `secrets.h.example`) with `WIFI_SSID`/`WIFI_PASS` (and optionally `HA_HOST`/`HA_PORT`/`HA_TOKEN`) to skip the portal and connect directly — handy for hidden SSIDs and iterating quickly.

---

## 📲 First‑time setup

1. On first boot the screen shows **“Setup required”** and the board opens a WiFi access point: **`HA‑Panel‑Setup`**.
2. Connect to it — the captive portal opens. Enter your WiFi, then your Home Assistant **host/IP**, **port**, a **long‑lived access token**, and tick **“Use HTTPS/WSS (TLS)”** if HA is exposed over HTTPS.
   _(Token: HA profile → Long‑lived access tokens → Create token.)_
3. The panel connects to HA and discovers `light` / `switch` / `sensor` entities.
4. Open **`http://<panel-ip>/`** to design your screens in the web editor (pick entities, arrange tiles, set sizes and labels).
5. Save — the dashboard appears. Toggles and sliders drive HA in real time; pull **down** for the settings menu.

---

## 🎛️ Web editor

The config portal hosts a **WYSIWYG** dashboard editor: the canvas is a scaled, pixel‑faithful replica of the device screen that adapts to each board's resolution. Click an entity to place it, drag tiles to reorder (with a live ghost + re‑pack), pick tile sizes (1×1 / 2×1 / 1×2 / 2×2), set per‑tile labels, and manage swipe pages — all rendered exactly as the panel will show them, even offline in AP mode.

---

## ➕ Adding a board

1. `src/board/profiles/<name>.h` — geometry, pins, display/touch drivers, expander.
2. A branch in `src/board/BoardConfig.h`.
3. An `[env:<name>]` in `platformio.ini` with `-D BOARD_<NAME>`.
4. New touch controller? Add an `ITouch` implementation in `Touch.cpp`.

No changes to `ui/`, `ha/` or `net/` are required.

---

## 🎨 Regenerating icons & fonts

Icons and accented text fonts are generated, not hand‑edited. After changing the icon set or text sizes:

```bash
cd scripts/mdi-font
npm install        # first time
npm run gen        # regenerates the MDI font, name↔glyph headers,
                   # the web SVG sprite, and the Montserrat FR fonts
```

To add an icon, append its MDI name to [`scripts/mdi-font/icons.txt`](scripts/mdi-font/icons.txt) and re‑run `npm run gen`.

---

## 🗺️ Roadmap

- ✅ **MVP** — lights (on/off + brightness), switches, and sensors (read‑only); real‑time WebSocket; multi‑page WYSIWYG editor; settings menu; OTA.
- 🔭 **Next** — color picker for RGB lights, scenes & scripts, more settings‑menu controls, additional Waveshare panels, single‑TLS refactor & portal auth hardening.

---

## 🙌 Built with

[LVGL](https://lvgl.io/) · [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) · [ArduinoJson](https://arduinojson.org/) · [WiFiManager](https://github.com/tzapu/WiFiManager) · [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) · [Material Design Icons](https://pictogrammers.com/library/mdi/) · [Montserrat](https://fonts.google.com/specimen/Montserrat)

---

## 📄 License

This project's source code is released under the **MIT License** — see [`LICENSE`](LICENSE).

Bundled fonts and icons keep their own licenses: **Montserrat** under the SIL Open Font License ([`OFL.txt`](OFL.txt)) and **Material Design Icons** under Apache-2.0. Full attribution for these and all dependencies is in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
