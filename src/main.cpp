/**
 * ESPanelHA — application entry point.
 *
 * Boot flow:
 *   HAL (display+touch) -> UI -> storage -> WiFi/HA provisioning ->
 *   HA WebSocket client -> entity-selection web portal -> dashboard.
 *
 * Execution model: a single cooperative loop (lv_timer_handler + ws.loop)
 * with no blocking calls, portable to the single-core C6 and the dual-core S3.
 */
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include <lvgl.h>
#include <math.h>
#include <time.h>

#include "board/BoardConfig.h"
#include "board/Display.h"
#include "board/Imu.h"
#include "board/Power.h"
#include "board/Touch.h"
#include "board/io/I2CBus.h"
#include "core/AppConfig.h"
#include "ha/EntityStore.h"
#include "ha/HAClient.h"
#include "net/ConfigPortal.h"
#include "net/Secure.h"
#include "net/Storage.h"
#include "net/WifiProvisioning.h"
#include "ui/UiManager.h"
#include "ui/UiTheme.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

core::AppConfig g_cfg;
ha::EntityStore g_store;
ha::HAClient g_client;

// Domains offered for selection in the portal and kept in the EntityStore.
// light/switch are controllable; sensor is display-only (handled in the UI).
const std::vector<String> kSelectableDomains = {"light", "switch", "sensor"};

// Set from the (async) web-portal save callbacks, applied in loop() so all LVGL
// and filesystem work stays on the main thread.
volatile bool g_pendingLayoutApply = false;
core::Layout g_pendingLayout;
volatile bool g_pendingHaApply = false;
core::HAConfig g_pendingHa;
volatile bool g_pendingAuthApply = false;
struct PendingAuth {
    bool enabled = false;
    String user;
    String password;  // empty -> keep the stored hash
};
PendingAuth g_pendingAuth;
volatile bool g_pendingDisplayApply = false;
core::DisplayConfig g_pendingDisplay;
volatile bool g_pendingPowerApply = false;
core::PowerConfig g_pendingPower;

// Queue an orientation/columns change; applied from loop() so all LVGL + flash
// work stays on the main thread (the web callback and IMU tick both use this).
void requestDisplayConfig(const core::DisplayConfig &d) {
    g_pendingDisplay = d;
    g_pendingDisplayApply = true;
}

// Queue a power-settings change (web portal + on-screen tiles), applied from
// loop() so persistence + brightness/sleep changes stay on the main thread.
void requestPowerConfig(const core::PowerConfig &p) {
    g_pendingPower = p;
    g_pendingPowerApply = true;
}

bool g_haStarted = false; // true once the HA WebSocket client has been begun

// Provisioning until WiFi connects; then Running (web portal up, HA may still
// need configuring via http://<ip>/).
enum class AppState { Provisioning, Running };
AppState g_state = AppState::Provisioning;

const char *statusText(ha::HAStatus s) {
    switch (s) {
        case ha::HAStatus::Disconnected:    return "HA: disconnected";
        case ha::HAStatus::Connecting:      return "HA: connecting";
        case ha::HAStatus::Authenticating:  return "HA: authenticating";
        case ha::HAStatus::Ready:           return "HA: connected";
        case ha::HAStatus::AuthFailed:      return "HA: auth failed";
    }
    return "";
}

void rebuildDashboard() {
    ui::uiShowDashboard(g_cfg.layout, g_store);
}

// Entity ids referenced by the layout — the only ones we keep full state for.
std::vector<String> layoutEntityIds(const core::Layout &layout) {
    std::vector<String> ids;
    for (const auto &p : layout.pages) {
        for (const auto &t : p.tiles) ids.push_back(t.entityId);
    }
    return ids;
}

// Start (or restart) the HA WebSocket client with the current config.
void startHomeAssistant() {
    if (!g_haStarted) {
        // Store only what the UI uses — HA's full state dump is ~200 KB and would
        // otherwise exhaust the heap (and starve TLS) on the initial REST fetch.
        g_store.setDomainFilter(kSelectableDomains);
        g_store.onChange([](const ha::EntityState &e) { ui::uiUpdateEntity(e); });
        // (interest set below, before each begin)
        g_client.onStatus([](ha::HAStatus s) {
            ui::uiSetConnectionStatus(statusText(s));
            if (s == ha::HAStatus::Ready) rebuildDashboard();
        });
        ui::Actions actions;
        actions.onToggle = [](const String &id) { g_client.toggle(id); };
        actions.onBrightness = [](const String &id, int pct) {
            g_client.lightSetBrightnessPct(id, pct);
        };
        actions.onCycleRotation = []() {
            core::DisplayConfig d = g_cfg.display;
            d.rotation = (d.rotation + 1) & 0x03;
            requestDisplayConfig(d);
        };
        actions.onToggleAutoRotate = [](bool on) {
            core::DisplayConfig d = g_cfg.display;
            d.autoRotate = on;
            requestDisplayConfig(d);
        };
        actions.onSetBrightness = [](int pct) {
            core::PowerConfig p = g_cfg.power;
            p.brightness = (uint8_t)(pct < 1 ? 1 : (pct > 100 ? 100 : pct));
            requestPowerConfig(p);
        };
        actions.onPreviewBrightness = [](int pct) {  // live, no persistence
            hal::displaySetBrightness((uint8_t)(pct < 1 ? 1 : (pct > 100 ? 100 : pct)));
        };
        actions.onToggleScreenSleep = [](bool on) {
            core::PowerConfig p = g_cfg.power;
            p.screenSleepEnabled = on;
            requestPowerConfig(p);
        };
        actions.onToggleDeepSleep = [](bool on) {
            core::PowerConfig p = g_cfg.power;
            p.deepSleepEnabled = on;
            requestPowerConfig(p);
        };
        ui::uiSetActions(actions);
    }
    // Keep full state only for the entities the dashboard shows (bounds heap on
    // large HA instances); everything selectable still lands in the catalog.
    g_store.setInterest(layoutEntityIds(g_cfg.layout));
    g_client.begin(g_cfg.ha, &g_store);
    g_haStarted = true;
}

// Serialize the device dashboard spec (geometry + style) for the web editor's
// WYSIWYG preview. Built here (composition root) so ui/ stays JSON-free; colors
// are emitted as "#rrggbb" for direct CSS use.
String deviceSpecJson() {
    const ui::DashboardSpec &s = ui::dashboardSpec();
    auto hex = [](uint32_t c) {
        char b[8];
        snprintf(b, sizeof(b), "#%06X", (unsigned)(c & 0xFFFFFF));
        return String(b);
    };
    JsonDocument doc;
    doc["board"] = s.boardName;
    doc["screenW"] = s.screenW;
    doc["screenH"] = s.screenH;
    doc["gridCols"] = s.gridCols;
    doc["rowHeightPx"] = s.rowHeightPx;
    doc["colGapPx"] = s.colGapPx;
    doc["rowGapPx"] = s.rowGapPx;
    doc["pagePadPx"] = s.pagePadPx;
    doc["pageTitleGapPx"] = s.pageTitleGapPx;
    doc["topBarHeightPx"] = s.topBarHeightPx;
    doc["dotsBarHeightPx"] = s.dotsBarHeightPx;
    doc["tileRadiusPx"] = s.tileRadiusPx;
    doc["tilePadPx"] = s.tilePadPx;
    doc["tileGapPx"] = s.tileGapPx;
    doc["iconSizePx"] = s.iconSizePx;
    doc["nameFontPx"] = s.nameFontPx;
    doc["stateFontPx"] = s.stateFontPx;
    doc["titleFontPx"] = s.titleFontPx;
    doc["sliderHeightPx"] = s.sliderHeightPx;
    doc["valueFontSmallPx"] = s.valueFontSmallPx;
    doc["valueFontMedPx"] = s.valueFontMedPx;
    doc["valueFontLargePx"] = s.valueFontLargePx;
    doc["screenBg"] = hex(s.screenBg);
    doc["tileBg"] = hex(s.tileBg);
    doc["pressedBg"] = hex(s.pressedBg);
    doc["iconOff"] = hex(s.iconOff);
    doc["amber"] = hex(s.amber);
    doc["primary"] = hex(s.primary);
    doc["textMuted"] = hex(s.textMuted);
    doc["nameColor"] = hex(s.nameColor);
    doc["activeMix"] = s.activeMix;

    // Orientation + grid density (screenW/H/gridCols above already reflect the
    // active orientation). maxCols* bound the per-orientation column pickers.
    doc["rotation"] = g_cfg.display.rotation;
    doc["autoRotate"] = g_cfg.display.autoRotate;
#if defined(BOARD_HAS_IMU)
    doc["hasImu"] = true;
#else
    doc["hasImu"] = false;
#endif
    doc["colsPortrait"] = g_cfg.display.colsPortrait;
    doc["colsLandscape"] = g_cfg.display.colsLandscape;
    const int portraitW = s.screenW < s.screenH ? s.screenW : s.screenH;
    const int landscapeW = s.screenW > s.screenH ? s.screenW : s.screenH;
    doc["maxColsPortrait"] = ui::maxColsForWidth(s, portraitW, core::kMaxGridCols);
    doc["maxColsLandscape"] = ui::maxColsForWidth(s, landscapeW, core::kMaxGridCols);

    // Live battery/power status for the portal's read-only line (Power card).
    doc["hasPmic"] = hal::powerAvailable();
    doc["batteryPresent"] = hal::batteryPresent();
    doc["onBattery"] = hal::onBattery();
    doc["batteryCharging"] = hal::batteryCharging();
    doc["batteryPct"] = hal::batteryPercent();

    String out;
    serializeJson(doc, out);
    return out;
}

void startConfigPortal() {
    net::ConfigPortalCallbacks cb;
    cb.currentHa = []() { return g_cfg.ha; };
    cb.onSaveHa = [](core::HAConfig ha) {
        g_pendingHa = ha;       // applied on the main loop
        g_pendingHaApply = true;
    };
    // Whole selectable-entity catalog as one JSON blob; the web editor caches it
    // and filters client-side (no per-keystroke device work, no big RAM list).
    cb.entitiesCatalogJson = []() { return g_store.catalogJson(); };
    cb.deviceSpecJson = []() { return deviceSpecJson(); };
    cb.currentLayout = []() { return g_cfg.layout; };
    cb.onSaveLayout = [](core::Layout layout) {
        g_pendingLayout = std::move(layout);
        g_pendingLayoutApply = true;
    };
    cb.onSaveAuth = [](bool enabled, String user, String password) {
        g_pendingAuth = {enabled, std::move(user), std::move(password)};
        g_pendingAuthApply = true;
    };
    cb.currentDisplay = []() { return g_cfg.display; };
    cb.onSaveDisplay = [](core::DisplayConfig d) { requestDisplayConfig(d); };
    cb.currentPower = []() { return g_cfg.power; };
    cb.onSavePower = [](core::PowerConfig p) { requestPowerConfig(p); };
    cb.onFactoryReset = []() { net::clearAll(); };  // portal arms the reboot

    net::PortalAuth pa{g_cfg.auth.enabled, g_cfg.auth.user, g_cfg.auth.salt,
                       g_cfg.auth.hash};
    net::configPortalBegin(cb, pa);
}

// IMU-driven auto-rotation: map gravity to one of the 4 rotations with simple
// hysteresis and queue the change. Held when lying flat. The axis/sign mapping
// is panel-specific — calibrate on hardware (log ax/ay per physical orientation).
uint8_t imuRotationTarget(float ax, float ay) {
    if (fabsf(ax) >= fabsf(ay)) return ax > 0 ? 0 : 2;
    return ay > 0 ? 1 : 3;
}

void autoRotateTick() {
    if (!g_cfg.display.autoRotate || !hal::imuAvailable()) return;
    static uint32_t last = 0;
    if (millis() - last < 150) return;  // throttle the I2C reads
    last = millis();
    float ax, ay, az;
    if (!hal::imuRead(ax, ay, az)) return;
    if (fabsf(az) > 0.85f) return;  // lying flat: keep the current orientation
    const uint8_t target = imuRotationTarget(ax, ay);
    static uint8_t pending = 0xFF;
    static int stable = 0;
    if (target != pending) { pending = target; stable = 0; }
    else if (stable < 3) stable++;
    if (stable >= 3 && target != g_cfg.display.rotation) {
        core::DisplayConfig d = g_cfg.display;
        d.rotation = target;
        requestDisplayConfig(d);
    }
}

// ---- Power management -----------------------------------------------------

// True when the local clock is inside the configured quiet-hours window. Returns
// false until SNTP has set the clock, so a wrong window never blanks the screen.
bool inQuietHours() {
    const core::PowerConfig &p = g_cfg.power;
    if (!p.quietHoursEnabled || p.quietStartHour == p.quietEndHour) return false;
    time_t now = time(nullptr);
    if (now < 1000000000) return false;  // clock not set yet
    struct tm lt;
    localtime_r(&now, &lt);
    const int h = lt.tm_hour;
    if (p.quietStartHour < p.quietEndHour)
        return h >= p.quietStartHour && h < p.quietEndHour;
    return h >= p.quietStartHour || h < p.quietEndHour;  // wraps midnight
}

// Blank the panel, arm a touch wake (where the INT line is RTC-capable) and enter
// deep sleep. The chip reboots on wake, so the app restarts from setup().
void enterDeepSleep() {
#if defined(BOARD_DEEP_SLEEP_WAKE_GPIO)
    Serial.println("[power] entering deep sleep (battery idle)");
    hal::displaySleep();
    delay(40);  // let the panel command flush before the rails drop
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_DEEP_SLEEP_WAKE_GPIO, 0);  // INT active-low
    esp_deep_sleep_start();
#endif
}

// Two-stage idle handling: dim/blank the screen after the screen-sleep delay (or
// during quiet hours), then deep-sleep after the longer delay when on battery.
void powerTick() {
    const core::PowerConfig &p = g_cfg.power;
    const uint32_t idle = lv_disp_get_inactive_time(NULL);

    // During quiet hours, force a short auto-off even if screen-sleep is disabled,
    // so a touch still wakes the panel briefly before it blanks again.
    uint32_t offMs = p.screenSleepMs();
    if (inQuietHours()) {
        const uint32_t quietMs = 8000;
        offMs = (offMs == 0) ? quietMs : (offMs < quietMs ? offMs : quietMs);
    }

    if (offMs && idle >= offMs) {
        if (!hal::displayIsAsleep()) hal::displaySleep();
    } else if (hal::displayIsAsleep()) {
        hal::displayWake();
        ui::uiNotifyWake();  // swallow the tap that woke the screen
    }

    // Deep sleep only on battery, after the longer idle threshold.
    const uint32_t dsMs = p.deepSleepMs();
    if (dsMs && idle >= dsMs && hal::onBattery()) enterDeepSleep();
}

// Push battery state to HA as a sensor via the REST API (opt-in, throttled).
// Blocking call kept short and infrequent; see open-work-items (non-blocking fetch).
void reportBatteryToHa() {
    const core::PowerConfig &p = g_cfg.power;
    if (!p.reportBatteryToHa || !g_cfg.ha.isComplete() || !WiFi.isConnected()) return;
    const int pct = hal::batteryPercent();
    if (pct < 0) return;
    static uint32_t last = 0;
    if (last && millis() - last < 60000) return;  // at most once a minute
    last = millis();

    String url = (g_cfg.ha.useTls ? "https://" : "http://") + g_cfg.ha.host + ":" +
                 String(g_cfg.ha.port) + "/api/states/" + p.batteryEntity;
    String body = String("{\"state\":") + pct +
                  ",\"attributes\":{\"device_class\":\"battery\","
                  "\"unit_of_measurement\":\"%\",\"friendly_name\":\"ESPanelHA Battery\"}}";

    HTTPClient http;
    http.setConnectTimeout(1500);
    http.setTimeout(2500);
    bool begun;
    if (g_cfg.ha.useTls) {
        static WiFiClientSecure tls;
        tls.setInsecure();  // matches the firmware's insecure-TLS mode
        begun = http.begin(tls, url);
    } else {
        begun = http.begin(url);
    }
    if (!begun) return;
    http.addHeader("Authorization", "Bearer " + g_cfg.ha.token);
    http.addHeader("Content-Type", "application/json");
    http.POST(body);
    http.end();
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(300); // give USB-CDC a moment to enumerate so early logs aren't lost
    Serial.printf("\n[boot] %s\n", BOARD_NAME);

#if defined(DEBUG_I2C_SCAN)
    // Bring-up helper: confirms the expander/touch addresses on real hardware.
    hal::boardI2CScan();
#endif

    lv_init();
    hal::displayInit();
    hal::touchInit();
    hal::imuBegin();   // no-op on boards without an IMU
    hal::powerBegin(); // no-op on boards without a PMIC

    // Load config before building the UI so the dashboard lays out at the stored
    // orientation/columns from the first frame (no rebuild on boot).
    net::storageBegin();
    net::loadConfig(g_cfg);

    ui::uiSetDisplayConfig(g_cfg.display);
    hal::displaySetRotation(g_cfg.display.rotation);
    hal::touchSetRotation(g_cfg.display.rotation);
    hal::displaySetBrightness(g_cfg.power.brightness);  // apply stored brightness

    ui::uiInit();
    ui::uiSetPowerConfig(g_cfg.power);  // reflect brightness slider + sleep toggles
    ui::uiShowBoot("Starting...");

#if defined(HA_HOST)
    // Compile-time HA settings (secrets.h) take precedence — skips portal entry.
    g_cfg.ha.host = HA_HOST;
    g_cfg.ha.port = HA_PORT;
    g_cfg.ha.token = HA_TOKEN;
#endif

    // Non-blocking: the portal runs from loop() so the UI keeps rendering.
    net::wifiBegin(g_cfg);
    ui::uiShowSetup(net::wifiPortalApName());
}

// Pick the screen to show based on what is configured.
void refreshScreen(const String &ip) {
    ui::uiSetPortalUrl(ip);                  // keep the settings screen's URL current
    if (!g_cfg.ha.isComplete()) {
        ui::uiShowConfigureHa(ip);          // need HA host/token
    } else if (g_cfg.layout.empty()) {
        ui::uiShowSettings();               // no dashboard yet -> settings/info page
    } else {
        rebuildDashboard();                 // ready (swipe down for settings)
    }
}

void onWifiConnected() {
    const String ip = WiFi.localIP().toString();
    Serial.printf("[wifi] connected, device IP: %s  (portal: http://%s/)\n",
                  ip.c_str(), ip.c_str());

    net::wifiStopPortal();                  // free :80 from the WiFiManager portal
    // Local time for the quiet-hours window (Europe/Paris); harmless otherwise.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    startConfigPortal();                    // web config always reachable now
    if (g_cfg.ha.isComplete()) startHomeAssistant();
    refreshScreen(ip);

    g_state = AppState::Running;
    Serial.println("[app] entering RUNNING state");
}

void loop() {
    lv_timer_handler(); // always: keeps the UI rendering in every state

    if (g_state == AppState::Provisioning) {
        net::wifiLoop();
        if (net::wifiConnected()) onWifiConnected();
        delay(2);
        return;
    }

    // Running.
    net::configPortalLoop();  // services the deferred reboot after an OTA upload
    if (g_haStarted) g_client.loop();

    // Refresh the settings-menu WiFi-to-AP signal indicator (throttled, cheap).
    static uint32_t lastWifiPush = 0;
    if (millis() - lastWifiPush > 3000) {
        lastWifiPush = millis();
        ui::uiSetWifiStatus(WiFi.isConnected(), WiFi.RSSI());
    }

    // Refresh the battery indicator (and push to HA if enabled), throttled.
    static uint32_t lastBatPush = 0;
    if (millis() - lastBatPush > 5000) {
        lastBatPush = millis();
        ui::uiSetBatteryStatus(hal::batteryPresent(), hal::onBattery(),
                               hal::batteryPercent(), hal::batteryCharging());
        reportBatteryToHa();
    }

    autoRotateTick();  // queues a rotation change when the IMU says so
    powerTick();       // screen-off / deep-sleep / quiet-hours handling

    if (g_pendingHaApply) {
        g_pendingHaApply = false;
        g_cfg.ha = g_pendingHa;
        net::saveHAConfig(g_cfg.ha);
        if (g_cfg.ha.isComplete()) startHomeAssistant();  // begin or reconnect
        refreshScreen(WiFi.localIP().toString());
    }

    if (g_pendingLayoutApply) {
        g_pendingLayoutApply = false;
        g_cfg.layout = g_pendingLayout;
        net::saveLayout(g_cfg.layout);
        // Re-snapshot so newly added tiles get their full state (updates the
        // interest set + repopulates over a single TLS session).
        if (g_haStarted && g_cfg.ha.isComplete()) startHomeAssistant();
        refreshScreen(WiFi.localIP().toString());
    }

    if (g_pendingAuthApply) {
        g_pendingAuthApply = false;
        core::AuthConfig a = g_cfg.auth;
        a.user = g_pendingAuth.user;
        if (g_pendingAuth.password.length()) {  // rehash only when a new one is set
            a.salt = net::randomHex(16);
            a.hash = net::sha256Hex(a.salt, g_pendingAuth.password);
        }
        // Can't enable auth without a username and a stored hash.
        const bool haveCreds = a.user.length() > 0 && a.hash.length() > 0;
        a.enabled = g_pendingAuth.enabled && haveCreds;
        g_cfg.auth = a;
        net::saveAuth(a);
        net::configPortalSetAuth({a.enabled, a.user, a.salt, a.hash});
    }

    if (g_pendingDisplayApply) {
        g_pendingDisplayApply = false;
        const core::DisplayConfig prev = g_cfg.display;
        g_cfg.display = g_pendingDisplay;
        net::saveDisplayConfig(g_cfg.display);
        ui::uiSetDisplayConfig(g_cfg.display);  // spec + auto-rotate switch state

        // Only re-rotate + rebuild when the geometry actually changed; a plain
        // auto-rotate toggle leaves the current screen untouched.
        const bool geomChanged =
            prev.rotation != g_cfg.display.rotation || prev.cols() != g_cfg.display.cols();
        if (geomChanged) {
            hal::displaySetRotation(g_cfg.display.rotation);
            hal::touchSetRotation(g_cfg.display.rotation);
            ui::uiApplyOrientation();
            refreshScreen(WiFi.localIP().toString());
            ui::uiSetWifiStatus(WiFi.isConnected(), WiFi.RSSI());  // rebuilt icon
        }
    }

    if (g_pendingPowerApply) {
        g_pendingPowerApply = false;
        g_cfg.power = g_pendingPower;
        net::savePowerConfig(g_cfg.power);
        hal::displaySetBrightness(g_cfg.power.brightness);
        hal::displayWake();              // make a brightness change visible at once
        ui::uiSetPowerConfig(g_cfg.power);  // sync slider + sleep toggles
    }

    delay(2); // yield to WiFi / async-TCP tasks and feed the watchdog
}
