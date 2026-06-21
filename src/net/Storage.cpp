#include "Storage.h"

#include "Secure.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace net {

namespace {
constexpr const char *kNvsNamespace = "hapanel";
constexpr const char *kEntitiesPath = "/entities.json";  // legacy (migrated away)
constexpr const char *kLayoutPath = "/layout.json";
Preferences prefs;

// A missing key OR a stray literal "null" string (persisted by an earlier
// serialization bug, where as<String>() on an absent key yielded "null") both
// mean "no value". Avoids showing "null" as an entity/page name on screen.
String jsonStr(JsonVariantConst v) {
    String s = v | "";
    if (s == "null") s = "";
    return s;
}

void readLayoutFile(core::Layout &layout) {
    layout.pages.clear();
    File f = LittleFS.open(kLayoutPath, "r");
    if (!f) return;

    JsonDocument doc;
    const bool ok = deserializeJson(doc, f) == DeserializationError::Ok;
    f.close();
    if (!ok) return;

    for (JsonObject p : doc["pages"].as<JsonArray>()) {
        core::LayoutPage page;
        page.title = jsonStr(p["title"]);
        for (JsonObject t : p["tiles"].as<JsonArray>()) {
            core::LayoutTile tile;
            tile.entityId = t["id"].as<String>();
            tile.label = jsonStr(t["label"]);
            tile.w = t["w"] | 1;
            tile.h = t["h"] | 1;
            if (tile.w < 1 || tile.w > 2) tile.w = 1;
            if (tile.h < 1 || tile.h > 2) tile.h = 1;
            page.tiles.push_back(tile);
        }
        layout.pages.push_back(page);
    }
}

// Wrap a legacy flat /entities.json into a one-page layout, then persist it.
void migrateEntitiesToLayout(core::Layout &layout) {
    if (!LittleFS.exists(kEntitiesPath)) return;  // nothing to migrate
    File f = LittleFS.open(kEntitiesPath, "r");
    if (!f) return;

    JsonDocument doc;
    const bool ok = deserializeJson(doc, f) == DeserializationError::Ok;
    f.close();
    if (!ok) return;

    core::LayoutPage page;  // empty title -> no on-screen title bar
    for (JsonObject e : doc["entities"].as<JsonArray>()) {
        core::LayoutTile tile;
        tile.entityId = e["id"].as<String>();
        tile.label = jsonStr(e["label"]);
        page.tiles.push_back(tile);
    }
    if (!page.tiles.empty()) {
        layout.pages.push_back(page);
        saveLayout(layout);
    }
}
} // namespace

bool storageBegin() {
    // format-on-fail so a fresh board comes up usable.
    return LittleFS.begin(true);
}

void loadConfig(core::AppConfig &out) {
    prefs.begin(kNvsNamespace, true /*readOnly*/);
    // isKey() guards avoid the noisy "nvs_get_str ... NOT_FOUND" error logs that
    // Preferences prints for absent keys (harmless, but alarming on a fresh NVS).
    auto str = [](const char *key) {
        return prefs.isKey(key) ? prefs.getString(key, "") : String("");
    };
    out.ha.host = str("ha_host");
    out.ha.port = prefs.getUShort("ha_port", 8123);
    out.ha.useTls = prefs.getBool("ha_tls", false);
    // Prefer the encrypted token; fall back to a legacy plaintext token (it gets
    // re-stored encrypted on the next save).
    const String enc = str("ha_token_enc");
    out.ha.token = enc.length() ? decryptToken(enc) : str("ha_token");

    out.auth.enabled = prefs.getBool("auth_en", false);
    out.auth.user = str("auth_user");
    out.auth.salt = str("auth_salt");
    out.auth.hash = str("auth_hash");
    prefs.end();

    if (LittleFS.exists(kLayoutPath)) {
        readLayoutFile(out.layout);
    } else {
        migrateEntitiesToLayout(out.layout);  // no-op if no legacy file
    }
}

void saveHAConfig(const core::HAConfig &ha) {
    prefs.begin(kNvsNamespace, false);
    prefs.putString("ha_host", ha.host);
    prefs.putUShort("ha_port", ha.port);
    prefs.putBool("ha_tls", ha.useTls);
    prefs.putString("ha_token_enc", encryptToken(ha.token));
    prefs.remove("ha_token");  // drop any legacy plaintext token
    prefs.end();
}

void saveLayout(const core::Layout &layout) {
    JsonDocument doc;
    doc["v"] = 2;
    JsonArray pages = doc["pages"].to<JsonArray>();
    size_t pageCount = 0;
    for (const auto &p : layout.pages) {
        if (pageCount++ >= core::kMaxPages) break;
        JsonObject po = pages.add<JsonObject>();
        po["title"] = p.title;
        JsonArray tiles = po["tiles"].to<JsonArray>();
        size_t tileCount = 0;
        for (const auto &t : p.tiles) {
            if (tileCount++ >= core::kMaxTilesPerPage) break;
            JsonObject to = tiles.add<JsonObject>();
            to["id"] = t.entityId;
            if (t.label.length()) to["label"] = t.label;  // omit empty (no "null")
            to["w"] = t.w;
            to["h"] = t.h;
        }
    }

    File f = LittleFS.open(kLayoutPath, "w");
    if (!f) {
        Serial.println("[storage] failed to open layout.json for write");
        return;
    }
    serializeJson(doc, f);
    f.close();
}

void saveAuth(const core::AuthConfig &auth) {
    prefs.begin(kNvsNamespace, false);
    prefs.putBool("auth_en", auth.enabled);
    prefs.putString("auth_user", auth.user);
    prefs.putString("auth_salt", auth.salt);
    prefs.putString("auth_hash", auth.hash);
    prefs.end();
}

void clearAll() {
    prefs.begin(kNvsNamespace, false);
    prefs.clear();
    prefs.end();
    LittleFS.remove(kEntitiesPath);
    LittleFS.remove(kLayoutPath);
}

} // namespace net
