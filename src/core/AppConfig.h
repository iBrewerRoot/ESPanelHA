/**
 * Application configuration shared across the net / ha / ui layers.
 * Persisted by net::Storage:
 *   - HA credentials in NVS (token encrypted at rest),
 *   - config-portal auth (hash + salt) in NVS,
 *   - the dashboard layout (pages + tiles) in /layout.json on LittleFS.
 */
#ifndef CORE_APP_CONFIG_H
#define CORE_APP_CONFIG_H

#include <Arduino.h>
#include <vector>

namespace core {

// Bounds on the layout so a misconfigured device can't exhaust the heap when
// realizing widgets (the C6 in particular has no PSRAM). Enforced on save.
constexpr size_t kMaxPages = 8;
constexpr size_t kMaxTilesPerPage = 12;

// Upper bound on the grid column count (per orientation). The actual maximum is
// auto-detected from the screen width; this just caps it for any panel.
constexpr uint8_t kMaxGridCols = 4;

/** Home Assistant connection settings (entered via the web portal). */
struct HAConfig {
    String host;        // hostname or IP, e.g. "homeassistant.local" or "192.168.1.10"
    uint16_t port = 8123;
    String token;       // long-lived access token (stored encrypted)
    bool useTls = false; // true -> wss:// (TLS), false -> ws:// (plaintext)

    bool isComplete() const { return host.length() > 0 && token.length() > 0; }
};

/** A controllable/displayable entity discovered from HA, offered in the portal. */
struct AvailableEntity {
    String entityId;
    String friendlyName;
    String domain;      // "light", "switch", "sensor", ...
};

/** One tile on a page: which entity, an optional label override, and its span
 *  on the on-screen grid. w is a column span (clamped to the current column
 *  count at render time); h is a row span in {1,2}. */
struct LayoutTile {
    String entityId;    // e.g. "light.kitchen"
    String label;       // user override (empty -> HA friendly_name)
    uint8_t w = 1;      // column span (1 = one cell, up to the column count)
    uint8_t h = 1;      // row span (1 or 2)
};

/** A swipeable page. An empty title hides the on-screen title bar. */
struct LayoutPage {
    String title;
    std::vector<LayoutTile> tiles;
};

/** The whole dashboard: an ordered list of pages. */
struct Layout {
    std::vector<LayoutPage> pages;

    bool empty() const {
        for (const auto &p : pages) {
            if (!p.tiles.empty()) return false;
        }
        return true;
    }
};

/** Config-portal HTTP Basic auth. Password is never stored; only a salted
 *  SHA-256 hash is kept, so a flash dump does not reveal it. */
struct AuthConfig {
    bool enabled = false;
    String user;
    String salt;        // hex, random per device
    String hash;        // hex, SHA-256(salt || password)
};

/** Screen orientation + grid density. Persisted in NVS. `rotation` is the
 *  Arduino_GFX rotation index (0/2 = portrait, 1/3 = landscape); the column
 *  count applied depends on whether the current rotation is portrait or
 *  landscape. `autoRotate` is honored only on boards with an IMU. */
struct DisplayConfig {
    uint8_t rotation = 0;        // 0..3
    bool autoRotate = false;     // IMU-driven (ignored when no sensor)
    uint8_t colsPortrait = 2;    // grid columns when portrait
    uint8_t colsLandscape = 3;   // grid columns when landscape

    bool isLandscape() const { return rotation & 1; }
    uint8_t cols() const { return isLandscape() ? colsLandscape : colsPortrait; }
};

struct AppConfig {
    HAConfig ha;
    Layout layout;
    AuthConfig auth;
    DisplayConfig display;
};

} // namespace core

#endif /* CORE_APP_CONFIG_H */
