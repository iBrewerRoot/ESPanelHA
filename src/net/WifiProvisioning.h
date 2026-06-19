/**
 * WiFi provisioning via WiFiManager, in NON-BLOCKING mode.
 *
 * On first boot (or when no WiFi credentials are stored) the device starts an
 * access point + captive portal. The portal also collects the Home Assistant
 * host / port / token as custom fields, persisted on save.
 *
 * Non-blocking is essential: the LVGL UI must keep rendering (lv_timer_handler
 * runs in the main loop) while the user is on the portal. wifiBegin() returns
 * immediately; wifiLoop() must be called every iteration until connected.
 */
#ifndef NET_WIFI_PROVISIONING_H
#define NET_WIFI_PROVISIONING_H

#include "core/AppConfig.h"

namespace net {

/** Start WiFi (connect or open the portal). Non-blocking. `cfg` must outlive
 *  the provisioning phase: HA fields entered in the portal are written into it. */
void wifiBegin(core::AppConfig &cfg);

/** Drive the WiFiManager portal state machine. Call every loop iteration. */
void wifiLoop();

/** True once associated to an AP in station mode. */
bool wifiConnected();

/** AP name advertised by the captive portal. */
const char *wifiPortalApName();

} // namespace net

#endif /* NET_WIFI_PROVISIONING_H */
