/**
 * ESP32 Home Assistant Control Panel — application entry point.
 *
 * Boot flow:
 *   HAL (display+touch) -> UI -> storage -> WiFi/HA provisioning ->
 *   HA WebSocket client -> entity-selection web portal -> dashboard.
 *
 * Execution model: a single cooperative loop (lv_timer_handler + ws.loop)
 * with no blocking calls, portable to the single-core C6 and the dual-core S3.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>

#include "board/BoardConfig.h"
#include "board/Display.h"
#include "board/Touch.h"
#include "board/io/I2CBus.h"
#include "core/AppConfig.h"
#include "ha/EntityStore.h"
#include "ha/HAClient.h"
#include "net/ConfigPortal.h"
#include "net/Storage.h"
#include "net/WifiProvisioning.h"
#include "ui/UiManager.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

core::AppConfig g_cfg;
ha::EntityStore g_store;
ha::HAClient g_client;

// Controllable domains in scope for the MVP (sensors come in phase 2).
const std::vector<String> kControllableDomains = {"light", "switch"};

// Set from the (async) web-portal save callbacks, applied in loop() so all LVGL
// and filesystem work stays on the main thread.
volatile bool g_pendingSelectionApply = false;
std::vector<core::SelectedEntity> g_pendingSelection;
volatile bool g_pendingHaApply = false;
core::HAConfig g_pendingHa;

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
    ui::uiShowDashboard(g_cfg.entities, g_store);
}

// Start (or restart) the HA WebSocket client with the current config.
void startHomeAssistant() {
    if (!g_haStarted) {
        g_store.onChange([](const ha::EntityState &e) { ui::uiUpdateEntity(e); });
        g_client.onStatus([](ha::HAStatus s) {
            ui::uiSetConnectionStatus(statusText(s));
            if (s == ha::HAStatus::Ready) rebuildDashboard();
        });
        ui::Actions actions;
        actions.onToggle = [](const String &id) { g_client.toggle(id); };
        actions.onBrightness = [](const String &id, int pct) {
            g_client.lightSetBrightnessPct(id, pct);
        };
        ui::uiSetActions(actions);
    }
    g_client.begin(g_cfg.ha, &g_store);
    g_haStarted = true;
}

void startConfigPortal() {
    net::ConfigPortalCallbacks cb;
    cb.currentHa = []() { return g_cfg.ha; };
    cb.onSaveHa = [](core::HAConfig ha) {
        g_pendingHa = ha;       // applied on the main loop
        g_pendingHaApply = true;
    };
    cb.listAvailable = []() { return g_store.listByDomain(kControllableDomains); };
    cb.currentSelection = []() { return g_cfg.entities; };
    cb.onSave = [](std::vector<core::SelectedEntity> sel) {
        g_pendingSelection = std::move(sel);
        g_pendingSelectionApply = true;
    };
    net::configPortalBegin(cb);
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
    ui::uiInit();
    ui::uiShowBoot("Starting...");

    net::storageBegin();
    net::loadConfig(g_cfg);

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
    if (!g_cfg.ha.isComplete()) {
        ui::uiShowConfigureHa(ip);          // need HA host/token
    } else if (g_cfg.entities.empty()) {
        ui::uiShowConnected(ip);            // need to pick entities
    } else {
        rebuildDashboard();                 // ready
    }
}

void onWifiConnected() {
    const String ip = WiFi.localIP().toString();
    Serial.printf("[wifi] connected, device IP: %s  (portal: http://%s/)\n",
                  ip.c_str(), ip.c_str());

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
    if (g_haStarted) g_client.loop();

    if (g_pendingHaApply) {
        g_pendingHaApply = false;
        g_cfg.ha = g_pendingHa;
        net::saveHAConfig(g_cfg.ha);
        if (g_cfg.ha.isComplete()) startHomeAssistant();  // begin or reconnect
        refreshScreen(WiFi.localIP().toString());
    }

    if (g_pendingSelectionApply) {
        g_pendingSelectionApply = false;
        g_cfg.entities = g_pendingSelection;
        net::saveEntities(g_cfg.entities);
        rebuildDashboard();
    }

    delay(2); // yield to WiFi / async-TCP tasks and feed the watchdog
}
