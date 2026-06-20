/**
 * Home Assistant WebSocket client.
 *
 * Implements the native HA WebSocket API: auth -> get_states -> subscribe to
 * state_changed -> incremental updates into EntityStore. Sends commands via
 * call_service. Reconnects automatically (handled by WebSocketsClient).
 *
 * Cooperative: call loop() frequently from the main loop. No blocking calls,
 * so it is portable to the single-core C6.
 */
#ifndef HA_HACLIENT_H
#define HA_HACLIENT_H

#include "EntityStore.h"
#include "core/AppConfig.h"

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>

namespace ha {

enum class HAStatus { Disconnected, Connecting, Authenticating, Ready, AuthFailed };

class HAClient {
public:
    using StatusCb = std::function<void(HAStatus)>;

    void begin(const core::HAConfig &cfg, EntityStore *store);
    void loop();

    void onStatus(StatusCb cb) { statusCb_ = std::move(cb); }
    HAStatus status() const { return status_; }

    // Service calls for the controllable domains in scope for the MVP.
    void toggle(const String &entityId);
    void lightSetBrightnessPct(const String &entityId, int pct);

private:
    void onWsEvent(WStype_t type, uint8_t *payload, size_t length);
    void handleMessage(const char *payload, size_t length);
    void sendAuth();
    void subscribeStateChanged();
    uint32_t sendCommand(const String &json);
    void setStatus(HAStatus s);

    // Initial snapshot via REST (the WS get_states response exceeds the 15 KB
    // WebSocket frame limit). Streams /api/states and parses it one entity at a
    // time, so memory stays bounded regardless of instance size.
    bool fetchStatesViaRest();
    int parseStatesArray(Stream &s);

    WebSocketsClient ws_;
    EntityStore *store_ = nullptr;
    core::HAConfig cfg_;
    HAStatus status_ = HAStatus::Disconnected;
    StatusCb statusCb_;

    uint32_t nextId_ = 1;
};

} // namespace ha

#endif /* HA_HACLIENT_H */
