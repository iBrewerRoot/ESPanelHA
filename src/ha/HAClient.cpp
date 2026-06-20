#include "HAClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

namespace ha {

namespace {

/* Normalize a user-entered host: HA expects a bare hostname/IP, but users often
 * paste the full browser URL. Strip scheme, any path, and surrounding spaces. */
String sanitizeHost(const String &raw) {
    String h = raw;
    h.trim();
    int scheme = h.indexOf("://");
    if (scheme >= 0) h = h.substring(scheme + 3);
    int slash = h.indexOf('/');
    if (slash >= 0) h = h.substring(0, slash);  // drop path
    int colon = h.indexOf(':');
    if (colon >= 0) h = h.substring(0, colon);  // drop any :port (port field wins)
    h.trim();
    return h;
}

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

    // Presentation attributes — let the UI mirror HA's own icon/color/unit.
    if (attrs["icon"].is<const char *>()) out.icon = attrs["icon"].as<const char *>();
    if (attrs["unit_of_measurement"].is<const char *>())
        out.unit = attrs["unit_of_measurement"].as<const char *>();
    if (attrs["device_class"].is<const char *>())
        out.deviceClass = attrs["device_class"].as<const char *>();

    // rgb_color: [r, g, b] when a color light is on — packed into 0xRRGGBB.
    JsonArrayConst rgb = attrs["rgb_color"];
    if (rgb.size() == 3) {
        out.rgb = (rgb[0].as<int>() << 16) | (rgb[1].as<int>() << 8) | rgb[2].as<int>();
    }
    return true;
}

} // namespace

void HAClient::begin(const core::HAConfig &cfg, EntityStore *store) {
    cfg_ = cfg;
    cfg_.host = sanitizeHost(cfg_.host);
    store_ = store;
    Serial.printf("[ha] begin %s://%s:%u/api/websocket\n",
                  cfg_.useTls ? "wss" : "ws", cfg_.host.c_str(), cfg_.port);

    // Close any existing WS so its TLS context is freed before the REST snapshot
    // (keeps us to a single TLS session even when begin() is called again, e.g.
    // after a layout change).
    ws_.disconnect();

    // Single-TLS ordering: take the initial state snapshot over REST BEFORE
    // opening the WebSocket. The wss and https TLS sessions each need ~40 KB of
    // heap; holding both at once exhausts memory on large instances (the REST
    // parse was failing with IncompleteInput at ~8 KB free). Fetching first, then
    // letting the HTTPS client free its TLS context, leaves room for the wss one.
    setStatus(HAStatus::Connecting);
    fetchStatesViaRest();

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
}

void HAClient::loop() { ws_.loop(); }

void HAClient::onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            Serial.println("[ha] ws connected");
            setStatus(HAStatus::Authenticating);
            break;
        case WStype_DISCONNECTED:
            Serial.println("[ha] ws disconnected (will retry)");
            setStatus(HAStatus::Disconnected);
            break;
        case WStype_ERROR:
            Serial.printf("[ha] ws error: %.*s\n", (int)length,
                          payload ? reinterpret_cast<const char *>(payload) : "");
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
        Serial.println("[ha] auth_required -> sending token");
        sendAuth();
        return;
    }
    if (strcmp(type, "auth_ok") == 0) {
        Serial.println("[ha] auth_ok");
        subscribeStateChanged();
        // The snapshot is normally taken in begin() over a single TLS context.
        // Only fall back to a (dual-TLS) fetch here if that came back empty, e.g.
        // the network wasn't ready yet at begin().
        if (store_ && store_->snapshotEmpty()) fetchStatesViaRest();
        setStatus(HAStatus::Ready);
        return;
    }
    if (strcmp(type, "auth_invalid") == 0) {
        Serial.printf("[ha] auth_invalid: %s\n", doc["message"] | "(bad token)");
        setStatus(HAStatus::AuthFailed);
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

namespace {

// Wraps the HTTP(S) client so reads block until data arrives or the connection
// truly ends. Without this, TLS delivers the body in bursts and a transient
// empty read() looks like EOF to ArduinoJson (IncompleteInput).
class BlockingClientStream : public Stream {
public:
    explicit BlockingClientStream(WiFiClient &c, uint32_t timeoutMs = 8000)
        : client_(c), timeoutMs_(timeoutMs) {}
    int available() override { return client_.available(); }
    int read() override { return waitData() ? client_.read() : -1; }
    int peek() override { return waitData() ? client_.peek() : -1; }
    size_t write(uint8_t b) override { return client_.write(b); }
    size_t write(const uint8_t *b, size_t n) override { return client_.write(b, n); }

private:
    bool waitData() {
        const uint32_t start = millis();
        while (client_.available() == 0) {
            if (!client_.connected()) return false;  // server closed, no more data
            if (millis() - start > timeoutMs_) return false;
            delay(1);
        }
        return true;
    }
    WiFiClient &client_;
    uint32_t timeoutMs_;
};

// Block until a byte is readable, then return it without consuming (-1 on timeout).
int peekBlocking(Stream &s, uint32_t timeoutMs = 8000) {
    const uint32_t start = millis();
    while (s.available() == 0) {
        if (millis() - start > timeoutMs) return -1;
        delay(1);
    }
    return s.peek();
}

// Consume whitespace and element separators; peek the next meaningful byte.
int nextMeaningful(Stream &s) {
    while (true) {
        const int c = peekBlocking(s);
        if (c == -1) return -1;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',') {
            s.read();
            continue;
        }
        return c;
    }
}

} // namespace

// Parse a streamed JSON array of HA state objects, one element at a time, so
// only a single entity is held in memory at once.
int HAClient::parseStatesArray(Stream &s) {
    // Advance to the opening '['.
    while (true) {
        const int c = peekBlocking(s);
        if (c == -1) return -1;
        s.read();
        if (c == '[') break;
    }

    int n = 0;
    while (true) {
        const int c = nextMeaningful(s);
        if (c == -1) return -1;
        if (c == ']') { s.read(); break; }

        JsonDocument elem;  // freed each iteration -> bounded memory
        const DeserializationError err = deserializeJson(elem, s);
        if (err) {
            Serial.printf("[ha] rest parse error after %d entities: %s (heap=%u)\n",
                          n, err.c_str(), (unsigned)ESP.getFreeHeap());
            return -1;
        }
        EntityState e;
        if (parseState(elem.as<JsonObjectConst>(), e) && store_->ingest(e)) n++;
    }
    return n;
}

bool HAClient::fetchStatesViaRest() {
    WiFiClientSecure secure;
    WiFiClient plain;
    WiFiClient *net;
    if (cfg_.useTls) {
        secure.setInsecure();  // same trust model as the wss connection
        net = &secure;
    } else {
        net = &plain;
    }

    const String url = String(cfg_.useTls ? "https://" : "http://") + cfg_.host +
                       ":" + cfg_.port + "/api/states";
    HTTPClient http;
    if (!http.begin(*net, url)) {
        Serial.println("[ha] rest begin failed");
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + cfg_.token);
    http.useHTTP10(true);  // forces non-chunked body so the stream is clean JSON
    http.setTimeout(10000);

    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[ha] rest GET /api/states -> %d\n", code);
        http.end();
        return false;
    }

    BlockingClientStream stream(http.getStream());
    store_->beginCatalog();
    const int n = parseStatesArray(stream);
    store_->endCatalog();
    http.end();
    if (n < 0) {
        Serial.println("[ha] rest states: parse failed");
        return false;
    }
    Serial.printf("[ha] rest states: %d entities cataloged\n", n);
    return true;
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
    if (status_ != HAStatus::Ready) {
        Serial.printf("[ha] toggle %s dropped (status=%d, not ready)\n",
                      entityId.c_str(), (int)status_);
        return;
    }
    Serial.printf("[ha] toggle %s\n", entityId.c_str());
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
    if (status_ != HAStatus::Ready) {
        Serial.printf("[ha] brightness %s dropped (status=%d, not ready)\n",
                      entityId.c_str(), (int)status_);
        return;
    }
    Serial.printf("[ha] brightness %s = %d%%\n", entityId.c_str(), pct);
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
    Serial.printf("[ha] status -> %d\n", (int)s);
    if (statusCb_) statusCb_(s);
}

} // namespace ha
