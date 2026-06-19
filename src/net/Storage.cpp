#include "Storage.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace net {

namespace {
constexpr const char *kNvsNamespace = "hapanel";
constexpr const char *kEntitiesPath = "/entities.json";
Preferences prefs;
} // namespace

bool storageBegin() {
    // format-on-fail so a fresh board comes up usable.
    return LittleFS.begin(true);
}

void loadConfig(core::AppConfig &out) {
    prefs.begin(kNvsNamespace, true /*readOnly*/);
    out.ha.host = prefs.getString("ha_host", "");
    out.ha.port = prefs.getUShort("ha_port", 8123);
    out.ha.token = prefs.getString("ha_token", "");
    out.ha.useTls = prefs.getBool("ha_tls", false);
    prefs.end();

    out.entities.clear();
    File f = LittleFS.open(kEntitiesPath, "r");
    if (!f) return;

    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        for (JsonObject e : doc["entities"].as<JsonArray>()) {
            core::SelectedEntity se;
            se.entityId = e["id"].as<String>();
            se.label = e["label"].as<String>();
            out.entities.push_back(se);
        }
    }
    f.close();
}

void saveHAConfig(const core::HAConfig &ha) {
    prefs.begin(kNvsNamespace, false);
    prefs.putString("ha_host", ha.host);
    prefs.putUShort("ha_port", ha.port);
    prefs.putString("ha_token", ha.token);
    prefs.putBool("ha_tls", ha.useTls);
    prefs.end();
}

void saveEntities(const std::vector<core::SelectedEntity> &entities) {
    JsonDocument doc;
    JsonArray arr = doc["entities"].to<JsonArray>();
    for (const auto &e : entities) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = e.entityId;
        o["label"] = e.label;
    }
    File f = LittleFS.open(kEntitiesPath, "w");
    if (!f) {
        Serial.println("[storage] failed to open entities.json for write");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

void clearAll() {
    prefs.begin(kNvsNamespace, false);
    prefs.clear();
    prefs.end();
    LittleFS.remove(kEntitiesPath);
}

} // namespace net
