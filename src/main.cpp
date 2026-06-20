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
#include "net/Secure.h"
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
        ui::uiSetActions(actions);
    }
    // Keep full state only for the entities the dashboard shows (bounds heap on
    // large HA instances); everything selectable still lands in the catalog.
    g_store.setInterest(layoutEntityIds(g_cfg.layout));
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
    // Whole selectable-entity catalog as one JSON blob; the web editor caches it
    // and filters client-side (no per-keystroke device work, no big RAM list).
    cb.entitiesCatalogJson = []() { return g_store.catalogJson(); };
    cb.currentLayout = []() { return g_cfg.layout; };
    cb.onSaveLayout = [](core::Layout layout) {
        g_pendingLayout = std::move(layout);
        g_pendingLayoutApply = true;
    };
    cb.onSaveAuth = [](bool enabled, String user, String password) {
        g_pendingAuth = {enabled, std::move(user), std::move(password)};
        g_pendingAuthApply = true;
    };
    cb.onFactoryReset = []() { net::clearAll(); };  // portal arms the reboot

    net::PortalAuth pa{g_cfg.auth.enabled, g_cfg.auth.user, g_cfg.auth.salt,
                       g_cfg.auth.hash};
    net::configPortalBegin(cb, pa);
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
    } else if (g_cfg.layout.empty()) {
        ui::uiShowConnected(ip);            // need to pick entities
    } else {
        rebuildDashboard();                 // ready
    }
}

void onWifiConnected() {
    const String ip = WiFi.localIP().toString();
    Serial.printf("[wifi] connected, device IP: %s  (portal: http://%s/)\n",
                  ip.c_str(), ip.c_str());

    net::wifiStopPortal();                  // free :80 from the WiFiManager portal
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

    delay(2); // yield to WiFi / async-TCP tasks and feed the watchdog
}
