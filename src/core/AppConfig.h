/**
 * Application configuration shared across the net / ha / ui layers.
 * Persisted by net::Storage (HA credentials in NVS, entity selection in JSON).
 */
#ifndef CORE_APP_CONFIG_H
#define CORE_APP_CONFIG_H

#include <Arduino.h>
#include <vector>

namespace core {

/** Home Assistant connection settings (entered via the web portal). */
struct HAConfig {
    String host;        // hostname or IP, e.g. "homeassistant.local" or "192.168.1.10"
    uint16_t port = 8123;
    String token;       // long-lived access token
    bool useTls = false; // true -> wss:// (TLS), false -> ws:// (plaintext)

    bool isComplete() const { return host.length() > 0 && token.length() > 0; }
};

/** One entity the user chose to show on the dashboard, in display order. */
struct SelectedEntity {
    String entityId;    // e.g. "light.kitchen"
    String label;       // user-facing name (defaults to HA friendly_name)
};

/** A controllable entity discovered from HA, offered in the web portal. */
struct AvailableEntity {
    String entityId;
    String friendlyName;
    String domain;      // "light", "switch", ...
};

struct AppConfig {
    HAConfig ha;
    std::vector<SelectedEntity> entities;
};

} // namespace core

#endif /* CORE_APP_CONFIG_H */
