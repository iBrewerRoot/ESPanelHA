#include "WifiProvisioning.h"
#include "Storage.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace net {

namespace {
constexpr const char *kApName = "HA-Panel-Setup";

// These must persist for the whole portal lifetime (non-blocking mode keeps
// referencing them from wifiLoop), so they are file-static, not locals.
WiFiManager wm;
core::AppConfig *cfgPtr = nullptr;
WiFiManagerParameter *pHost = nullptr;
WiFiManagerParameter *pPort = nullptr;
WiFiManagerParameter *pToken = nullptr;
WiFiManagerParameter *pTls = nullptr;
char portBuf[6];

void onSaveParams() {
    if (!cfgPtr) return;
    cfgPtr->ha.host = pHost->getValue();
    cfgPtr->ha.port = static_cast<uint16_t>(atoi(pPort->getValue()));
    if (cfgPtr->ha.port == 0) cfgPtr->ha.port = 8123;
    cfgPtr->ha.token = pToken->getValue();
    cfgPtr->ha.useTls = (strlen(pTls->getValue()) > 0);
    saveHAConfig(cfgPtr->ha);
    Serial.println("[wifi] HA settings saved from portal");
}

} // namespace

const char *wifiPortalApName() { return kApName; }

// Log WiFi events — the disconnect reason code pinpoints why a connection
// fails (e.g. 201 NO_AP_FOUND => band/channel, 202/15 AUTH/handshake => password).
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[wifi] STA associated to AP");
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[wifi] got IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.printf("[wifi] STA disconnected, reason=%d\n",
                          info.wifi_sta_disconnected.reason);
            break;
        default:
            break;
    }
}

// Allow 2.4 GHz channels 1-13 (France/EU). Without this the default country
// code can hide networks on channels 12-13. ESP32 is 2.4 GHz only — 5 GHz
// networks remain invisible regardless of this setting.
void applyCountry() {
    WiFi.mode(WIFI_STA);
    wifi_country_t country = {};
    strncpy(country.cc, "FR", sizeof(country.cc));
    country.schan = 1;
    country.nchan = 13;
    country.policy = WIFI_COUNTRY_POLICY_MANUAL;
    esp_wifi_set_country(&country);
}

#if defined(DEBUG_WIFI_SCAN)
void wifiScanDebug() {
    const int n = WiFi.scanNetworks();
    Serial.printf("[wifi] scan: %d networks visible to the ESP32 (2.4 GHz only)\n", n);
    for (int i = 0; i < n; i++) {
        Serial.printf("[wifi]   ch%2d  %4d dBm  %s\n",
                      WiFi.channel(i), WiFi.RSSI(i), WiFi.SSID(i).c_str());
    }
    WiFi.scanDelete();
}
#endif

void wifiBegin(core::AppConfig &cfg) {
    cfgPtr = &cfg;

    WiFi.onEvent(onWifiEvent);
    applyCountry();
#if defined(DEBUG_WIFI_SCAN)
    wifiScanDebug();
#endif

#if defined(WIFI_SSID)
    // Compile-time credentials (secrets.h): connect directly. WiFi.begin sends a
    // directed probe, so this also works for hidden SSIDs the portal can't list.
    Serial.printf("[wifi] using compile-time credentials, connecting to '%s'\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    return;
#else
    snprintf(portBuf, sizeof(portBuf), "%u", cfg.ha.port);
    pHost = new WiFiManagerParameter("ha_host", "Home Assistant host/IP", cfg.ha.host.c_str(), 64);
    pPort = new WiFiManagerParameter("ha_port", "Port", portBuf, 6);
    pToken = new WiFiManagerParameter("ha_token", "Long-lived access token", cfg.ha.token.c_str(), 200);
    const char *tlsAttrs = cfg.ha.useTls ? "type=\"checkbox\" checked" : "type=\"checkbox\"";
    pTls = new WiFiManagerParameter("ha_tls", "Use HTTPS/WSS (TLS)", "T", 2, tlsAttrs, WFM_LABEL_AFTER);

    wm.addParameter(pHost);
    wm.addParameter(pPort);
    wm.addParameter(pToken);
    wm.addParameter(pTls);
    wm.setSaveParamsCallback(onSaveParams);

    // Non-blocking: the portal runs from wifiLoop() so the UI stays responsive.
    wm.setConfigPortalBlocking(false);

    // Returns immediately. If credentials exist it connects in the background;
    // otherwise it brings up the AP + captive portal.
    if (wm.autoConnect(kApName)) {
        Serial.println("[wifi] connected from stored credentials");
    } else {
        Serial.printf("[wifi] config portal started (AP: %s, http://192.168.4.1)\n", kApName);
    }
#endif // WIFI_SSID
}

void wifiLoop() {
#if !defined(WIFI_SSID)
    wm.process();
#endif
}

void wifiStopPortal() {
#if !defined(WIFI_SSID)
    // WiFiManager closes its own portal on connect (non-blocking, default
    // _disableConfigPortal). Only re-enter its shutdown if one is still active —
    // calling it once the server has been freed dereferences a null pointer and
    // crashes (WiFiManager.cpp:970).
    if (wm.getConfigPortalActive()) wm.stopConfigPortal();
    if (wm.getWebPortalActive()) wm.stopWebPortal();
    // Give lwIP time to release the portal's :80 listening socket before our
    // AsyncWebServer binds it; otherwise AsyncTCP returns "bind error: -8".
    delay(400);
#endif
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }

} // namespace net
