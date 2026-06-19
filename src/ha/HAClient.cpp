#include "HAClient.h"

#include <ArduinoJson.h>

namespace ha {

namespace {

/* Parse a HA state object (from get_states or a state_changed new_state) into
 * an EntityState. Returns false if it isn't a usable object. */
bool parseState(JsonObjectConst obj, EntityState &out) {
    if (obj.isNull()) return false;
    out.entityId = obj["entity_id"].as<const char *>();
    if (out.entityId.length() == 0) return false;
    out.domain = EntityStore::domainOf(out.entityId);
    out.state = obj["state"].as<const char *>();

    JsonObjectConst attrs = obj["attributes"];
    out.friendlyName = attrs["friendly_name"].is<const char *>()
                           ? attrs["friendly_name"].as<const char *>()
                           : out.entityId;
    out.brightness = attrs["brightness"].is<int>() ? attrs["brightness"].as<int>() : -1;
    return true;
}

} // namespace

void HAClient::begin(const core::HAConfig &cfg, EntityStore *store) {
    cfg_ = cfg;
    store_ = store;
    // TLS uses insecure mode (no cert validation) — fine on a trusted LAN and
    // for Nabu Casa; suitable for HA's typical self-signed certificates.
    if (cfg_.useTls) {
        ws_.beginSSL(cfg_.host.c_str(), cfg_.port, "/api/websocket");
    } else {
        ws_.begin(cfg_.host.c_str(), cfg_.port, "/api/websocket");
    }
    ws_.onEvent([this](WStype_t type, uint8_t *payload, size_t length) {
        onWsEvent(type, payload, length);
    });
    ws_.setReconnectInterval(5000);
    ws_.enableHeartbeat(15000, 3000, 2);
    setStatus(HAStatus::Connecting);
}

void HAClient::loop() { ws_.loop(); }

void HAClient::onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            setStatus(HAStatus::Authenticating);
            break;
        case WStype_DISCONNECTED:
            setStatus(HAStatus::Disconnected);
            break;
        case WStype_TEXT:
            handleMessage(reinterpret_cast<const char *>(payload), length);
            break;
        default:
            break;
    }
}

void HAClient::handleMessage(const char *payload, size_t length) {
    JsonDocument doc;
    if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

    const char *type = doc["type"] | "";

    if (strcmp(type, "auth_required") == 0) {
        sendAuth();
        return;
    }
    if (strcmp(type, "auth_ok") == 0) {
        requestStates();
        return;
    }
    if (strcmp(type, "auth_invalid") == 0) {
        setStatus(HAStatus::AuthFailed);
        return;
    }

    if (strcmp(type, "result") == 0) {
        const uint32_t id = doc["id"] | 0;
        if (id == getStatesId_ && doc["success"].as<bool>()) {
            for (JsonObjectConst s : doc["result"].as<JsonArrayConst>()) {
                EntityState e;
                if (parseState(s, e)) store_->update(e);
            }
            subscribeStateChanged();
            setStatus(HAStatus::Ready);
        }
        return;
    }

    if (strcmp(type, "event") == 0) {
        JsonObjectConst data = doc["event"]["data"];
        EntityState e;
        if (parseState(data["new_state"], e)) store_->update(e);
        return;
    }
}

void HAClient::sendAuth() {
    JsonDocument doc;
    doc["type"] = "auth";
    doc["access_token"] = cfg_.token;
    String out;
    serializeJson(doc, out);
    ws_.sendTXT(out);
}

void HAClient::requestStates() {
    getStatesId_ = nextId_++;
    JsonDocument doc;
    doc["id"] = getStatesId_;
    doc["type"] = "get_states";
    String out;
    serializeJson(doc, out);
    ws_.sendTXT(out);
}

void HAClient::subscribeStateChanged() {
    JsonDocument doc;
    doc["id"] = nextId_++;
    doc["type"] = "subscribe_events";
    doc["event_type"] = "state_changed";
    String out;
    serializeJson(doc, out);
    ws_.sendTXT(out);
}

uint32_t HAClient::sendCommand(const String &json) {
    String payload = json;  // WebSocketsClient::sendTXT takes a non-const ref
    ws_.sendTXT(payload);
    return nextId_;
}

void HAClient::toggle(const String &entityId) {
    if (status_ != HAStatus::Ready) return;
    JsonDocument doc;
    doc["id"] = nextId_++;
    doc["type"] = "call_service";
    doc["domain"] = EntityStore::domainOf(entityId);
    doc["service"] = "toggle";
    doc["target"]["entity_id"] = entityId;
    String out;
    serializeJson(doc, out);
    sendCommand(out);
}

void HAClient::lightSetBrightnessPct(const String &entityId, int pct) {
    if (status_ != HAStatus::Ready) return;
    pct = constrain(pct, 0, 100);
    JsonDocument doc;
    doc["id"] = nextId_++;
    doc["type"] = "call_service";
    doc["domain"] = "light";
    doc["service"] = pct == 0 ? "turn_off" : "turn_on";
    doc["target"]["entity_id"] = entityId;
    if (pct > 0) doc["service_data"]["brightness_pct"] = pct;
    String out;
    serializeJson(doc, out);
    sendCommand(out);
}

void HAClient::setStatus(HAStatus s) {
    if (s == status_) return;
    status_ = s;
    if (statusCb_) statusCb_(s);
}

} // namespace ha
