/**
 * LVGL UI layer.
 *
 * Resolution-agnostic: the dashboard is a responsive flex grid of entity cards
 * sized with percentages, so it reflows on every supported panel size.
 * Three screens: boot, setup (shown until configured), dashboard.
 */
#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#include "core/AppConfig.h"
#include "ha/EntityStore.h"

#include <Arduino.h>
#include <functional>
#include <vector>

namespace ui {

struct Actions {
    std::function<void(const String &entityId)> onToggle;
    std::function<void(const String &entityId, int pct)> onBrightness;
    std::function<void()> onCycleRotation;        // settings: rotate 90°
    std::function<void(bool enabled)> onToggleAutoRotate;  // settings: IMU toggle
    std::function<void(int pct)> onSetBrightness;         // settings: brightness slider released (persist)
    std::function<void(int pct)> onPreviewBrightness;     // settings: brightness slider dragging (live, no save)
    std::function<void(bool enabled)> onToggleScreenSleep;  // settings: screen-sleep toggle
    std::function<void(bool enabled)> onToggleDeepSleep;    // settings: deep-sleep toggle
};

void uiInit();
void uiSetActions(const Actions &actions);

/** Update the dashboard geometry (screen size + grid columns) from the display
 *  config. Call before uiInit() at boot, and before uiApplyOrientation() when
 *  the orientation/columns change. Does not rebuild widgets by itself. */
void uiSetDisplayConfig(const core::DisplayConfig &display);

/** Rebuild every screen at the current geometry (after an orientation change).
 *  The caller re-shows the appropriate screen afterwards. */
void uiApplyOrientation();

void uiShowBoot(const String &message);
void uiShowSetup(const String &apName);

/** Shown when WiFi is up but Home Assistant isn't configured yet: tells the
 *  user which address to open to enter the HA host/token. */
void uiShowConfigureHa(const String &ip);

/** Shown once WiFi/HA are connected but no entities are chosen yet: tells the
 *  user which address to open to pick entities. */
void uiShowConnected(const String &ip);

/** Build the swipeable, multi-page dashboard from the layout. Only the visible
 *  page's widgets are realized at a time (memory-conscious for the C6). */
void uiShowDashboard(const core::Layout &layout, const ha::EntityStore &store);

/** Refresh a single card from a new state (no-op if not on the dashboard). */
void uiUpdateEntity(const ha::EntityState &e);

/** Top-bar connection indicator text. */
void uiSetConnectionStatus(const String &text);

/** Update the WiFi-to-AP indicator on the settings menu (signal from RSSI). */
void uiSetWifiStatus(bool connected, int rssi);

/** Update the battery indicator (top-left of the title bars). The icon is hidden
 *  when there's no battery, or when running on external/USB power and not
 *  charging; the WiFi indicator shifts to make room when it's shown. */
void uiSetBatteryStatus(bool present, bool onBattery, int percent, bool charging);

/** Reflect the active brightness on the on-screen slider (1..100). */
void uiSetBrightness(int percent);

/** Sync the on-screen power controls (brightness slider + sleep toggles) from
 *  the stored config. Call at boot and after a power-settings change. */
void uiSetPowerConfig(const core::PowerConfig &power);

/** Tell the UI the screen was just woken by a touch, so the tap that woke it is
 *  swallowed (it must not toggle the tile underneath). */
void uiNotifyWake();

/** Remember the config-portal URL shown on the settings screen. */
void uiSetPortalUrl(const String &url);

/** Show the settings / info screen (config-portal URL + future controls). It
 *  appears automatically when no dashboard is configured, and is reachable from
 *  the dashboard with a swipe down (swipe up returns to the dashboard). */
void uiShowSettings();

} // namespace ui

#endif /* UI_UI_MANAGER_H */
