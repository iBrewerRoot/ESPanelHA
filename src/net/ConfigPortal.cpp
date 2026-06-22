#include "ConfigPortal.h"

#include "Secure.h"
#include "web_assets.h"  // generated: gzipped app.html/app.css/app.js byte arrays

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

namespace net {

namespace {

AsyncWebServer server(80);
ConfigPortalCallbacks callbacks;
PortalAuth g_auth;

constexpr size_t kMaxBodyBytes = 8192;   // hard cap on JSON request bodies

// Deferred restart — set from a handler, fired from configPortalLoop() so the
// HTTP response flushes before we reboot. Used by OTA and factory reset.
bool rebootArmed = false;
uint32_t rebootDeadline = 0;

void armReboot(uint32_t inMs) {
    rebootDeadline = millis() + inMs;
    rebootArmed = true;
}

// ---- Auth -----------------------------------------------------------------

// Verify HTTP Basic credentials against the cached salted hash. No response.
bool authPass(AsyncWebServerRequest *req) {
    if (!g_auth.enabled) return true;
    if (!req->hasHeader("Authorization")) return false;
    String header = req->getHeader("Authorization")->value();
    if (!header.startsWith("Basic ")) return false;

    String decoded;
    if (!base64Decode(header.substring(6), decoded)) return false;
    const int colon = decoded.indexOf(':');
    if (colon < 0) return false;
    const String user = decoded.substring(0, colon);
    const String pass = decoded.substring(colon + 1);

    return user == g_auth.user && sha256Hex(g_auth.salt, pass) == g_auth.hash;
}

// Verify or challenge with 401. Returns false if the request was rejected.
bool requireAuth(AsyncWebServerRequest *req) {
    if (authPass(req)) return true;
    req->requestAuthentication();
    return false;
}

// ---- Static assets (gzip, zero-copy from flash) ---------------------------

void sendGzip(AsyncWebServerRequest *req, const char *type,
              const uint8_t *data, size_t len) {
    // Zero-copy from flash; the byte-array overload streams the pointer directly.
    AsyncWebServerResponse *res = req->beginResponse(200, type, data, len);
    res->addHeader("Content-Encoding", "gzip");
    req->send(res);
}

// ---- JSON API: reads ------------------------------------------------------

void handleGetConfig(AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    core::HAConfig ha = callbacks.currentHa();
    JsonDocument doc;
    JsonObject h = doc["ha"].to<JsonObject>();
    h["host"] = ha.host;
    h["port"] = ha.port;
    h["useTls"] = ha.useTls;
    h["hasToken"] = ha.token.length() > 0;  // never expose the token itself
    JsonObject a = doc["auth"].to<JsonObject>();
    a["enabled"] = g_auth.enabled;
    a["user"] = g_auth.user;

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

void handleGetEntities(AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    // Pre-built catalog (one contiguous string); the browser filters it locally.
    req->send(200, "application/json", callbacks.entitiesCatalogJson());
}

void handleGetDevice(AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    // Board geometry + style tokens for the WYSIWYG editor preview.
    req->send(200, "application/json", callbacks.deviceSpecJson());
}

void handleGetLayout(AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;
    core::Layout layout = callbacks.currentLayout();

    JsonDocument doc;
    doc["maxPages"] = core::kMaxPages;
    doc["maxTilesPerPage"] = core::kMaxTilesPerPage;
    JsonArray pages = doc["pages"].to<JsonArray>();
    for (const auto &p : layout.pages) {
        JsonObject po = pages.add<JsonObject>();
        po["title"] = p.title;
        JsonArray tiles = po["tiles"].to<JsonArray>();
        for (const auto &t : p.tiles) {
            JsonObject to = tiles.add<JsonObject>();
            to["id"] = t.entityId;
            to["label"] = t.label;
            to["w"] = t.w;
            to["h"] = t.h;
        }
    }

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// ---- JSON API: writes -----------------------------------------------------

void handlePostHa(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!requireAuth(req)) return;
    JsonObject o = json.as<JsonObject>();
    core::HAConfig ha = callbacks.currentHa();  // keep fields not re-sent (token)
    if (o["host"].is<const char *>()) ha.host = o["host"].as<String>();
    if (o["port"].is<int>()) ha.port = o["port"].as<uint16_t>();
    if (ha.port == 0) ha.port = 8123;
    if (o["useTls"].is<bool>()) ha.useTls = o["useTls"].as<bool>();
    // Only overwrite the token when a non-empty one is sent.
    if (o["token"].is<const char *>()) {
        String tok = o["token"].as<String>();
        tok.trim();
        if (tok.length()) ha.token = tok;
    }
    callbacks.onSaveHa(ha);
    req->send(200, "application/json", "{\"ok\":true}");
}

void handlePostLayout(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!requireAuth(req)) return;
    core::Layout layout;
    size_t pageCount = 0;
    for (JsonObject p : json["pages"].as<JsonArray>()) {
        if (pageCount++ >= core::kMaxPages) break;
        core::LayoutPage page;
        page.title = p["title"].as<String>();
        size_t tileCount = 0;
        for (JsonObject t : p["tiles"].as<JsonArray>()) {
            if (tileCount++ >= core::kMaxTilesPerPage) break;
            core::LayoutTile tile;
            tile.entityId = t["id"].as<String>();
            tile.label = t["label"] | "";  // "" on missing key (web omits empty labels)
            tile.w = t["w"] | 1;
            tile.h = t["h"] | 1;
            if (tile.w < 1 || tile.w > core::kMaxGridCols) tile.w = 1;
            if (tile.h < 1 || tile.h > 2) tile.h = 1;
            if (tile.entityId.length()) page.tiles.push_back(tile);
        }
        layout.pages.push_back(page);
    }
    callbacks.onSaveLayout(layout);
    req->send(200, "application/json", "{\"ok\":true}");
}

void handlePostAuth(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!requireAuth(req)) return;
    const bool enabled = json["enabled"] | false;
    String user = json["user"].as<String>();
    String password = json["password"].as<String>();  // may be empty (keep hash)
    callbacks.onSaveAuth(enabled, user, password);
    req->send(200, "application/json", "{\"ok\":true}");
}

void handlePostDisplay(AsyncWebServerRequest *req, JsonVariant &json) {
    if (!requireAuth(req)) return;
    JsonObject o = json.as<JsonObject>();
    core::DisplayConfig d = callbacks.currentDisplay();  // keep fields not re-sent
    if (o["rotation"].is<int>()) d.rotation = (uint8_t)o["rotation"].as<int>() & 0x03;
    if (o["autoRotate"].is<bool>()) d.autoRotate = o["autoRotate"].as<bool>();
    if (o["colsPortrait"].is<int>()) d.colsPortrait = (uint8_t)o["colsPortrait"].as<int>();
    if (o["colsLandscape"].is<int>()) d.colsLandscape = (uint8_t)o["colsLandscape"].as<int>();
    auto clampCols = [](uint8_t c) -> uint8_t {
        if (c < 1) return 1;
        return c > core::kMaxGridCols ? core::kMaxGridCols : c;
    };
    d.colsPortrait = clampCols(d.colsPortrait);
    d.colsLandscape = clampCols(d.colsLandscape);
    callbacks.onSaveDisplay(d);
    req->send(200, "application/json", "{\"ok\":true}");
}

// ---- OTA (kept as a tiny inline page; no JS framework needed) -------------

String renderUpdatePage() {
    return F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Firmware</title><style>body{font-family:sans-serif;background:#111;"
        "color:#eee;margin:0;padding:16px}a{color:#03a9f4}button{padding:14px 20px;"
        "border:0;border-radius:8px;background:#03a9f4;color:#fff;width:100%;font-size:16px}"
        "input{width:100%;margin:8px 0;color:#eee}.bar{height:14px;background:#1b2128;"
        "border:1px solid #333;border-radius:7px;overflow:hidden;margin-top:12px}"
        ".bar>div{height:100%;width:0;background:#03a9f4;transition:width .2s}</style>"
        "</head><body><h3>Firmware update</h3>"
        "<p>Select a .bin built for this board. The panel reboots when done. Do not "
        "power off during the update.</p>"
        "<form id='f'><input type='file' id='file' accept='.bin'>"
        "<button type='submit'>Upload &amp; flash</button>"
        "<div class='bar'><div id='fill'></div></div><p id='msg'></p>"
        "<p><a href='/'>&larr; Back</a></p></form><script>"
        "var f=document.getElementById('f');f.onsubmit=function(e){e.preventDefault();"
        "var file=document.getElementById('file').files[0];if(!file)return;"
        "var fd=new FormData();fd.append('firmware',file,file.name);"
        "var x=new XMLHttpRequest();x.open('POST','/update');"
        "x.upload.onprogress=function(ev){if(ev.lengthComputable){"
        "var p=Math.round(ev.loaded/ev.total*100);"
        "document.getElementById('fill').style.width=p+'%';"
        "document.getElementById('msg').textContent=p+'%';}};"
        "x.onload=function(){document.getElementById('msg').textContent=x.responseText;};"
        "x.onerror=function(){document.getElementById('msg').textContent='Upload failed';};"
        "document.getElementById('msg').textContent='Uploading...';x.send(fd);};"
        "</script></body></html>");
}

bool uploadDenied = false;  // set if an OTA upload arrives without valid auth

void handleUpdateUpload(AsyncWebServerRequest *req, const String &filename,
                        size_t index, uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        uploadDenied = !authPass(req);  // refuse to flash without auth
        if (uploadDenied) return;
        Serial.printf("[ota] start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    }
    if (uploadDenied) return;
    if (Update.isRunning() && len) {
        if (Update.write(data, len) != len) Update.printError(Serial);
    }
    if (final && !uploadDenied) {
        if (Update.end(true)) {
            Serial.printf("[ota] success: %u bytes\n", (unsigned)(index + len));
        } else {
            Update.printError(Serial);
        }
    }
}

void handleUpdateDone(AsyncWebServerRequest *req) {
    if (!requireAuth(req)) return;  // also rejects the body written above
    const bool ok = !Update.hasError();
    AsyncWebServerResponse *res = req->beginResponse(
        ok ? 200 : 500, "text/plain",
        ok ? "Update OK \xE2\x80\x94 rebooting..." : "Update failed. Check the .bin and retry.");
    res->addHeader("Connection", "close");
    req->send(res);
    if (ok) armReboot(1500);
}

} // namespace

void configPortalSetAuth(const PortalAuth &auth) { g_auth = auth; }

void configPortalBegin(const ConfigPortalCallbacks &cb, const PortalAuth &auth) {
    callbacks = cb;
    g_auth = auth;

    // Static SPA assets (gzip, embedded in flash).
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        sendGzip(req, "text/html", kAppHtmlGz, kAppHtmlGzLen);
    });
    server.on("/app.css", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        sendGzip(req, "text/css", kAppCssGz, kAppCssGzLen);
    });
    server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        sendGzip(req, "application/javascript", kAppJsGz, kAppJsGzLen);
    });

    // Generated MDI icon sprite (gzip): injected by the editor for faithful icons.
    server.on("/mdi-sprite.svg", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        sendGzip(req, "image/svg+xml", kMdiSpriteGz, kMdiSpriteGzLen);
    });

    // Brand favicons + PWA icons + manifest. Public (no auth): browsers fetch
    // favicons automatically and these assets aren't sensitive.
    server.on("/wordmark.svg", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/svg+xml", kWordmarkGz, kWordmarkGzLen);
    });
    server.on("/favicon.svg", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/svg+xml", kFaviconSvgGz, kFaviconSvgGzLen);
    });
    server.on("/favicon-16.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kFavicon16Gz, kFavicon16GzLen);
    });
    server.on("/favicon-32.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kFavicon32Gz, kFavicon32GzLen);
    });
    server.on("/apple-touch-icon-180.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kAppleTouchGz, kAppleTouchGzLen);
    });
    server.on("/icon-192.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kIcon192Gz, kIcon192GzLen);
    });
    server.on("/icon-512.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kIcon512Gz, kIcon512GzLen);
    });
    server.on("/maskable-512.png", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "image/png", kMaskable512Gz, kMaskable512GzLen);
    });
    server.on("/site.webmanifest", HTTP_GET, [](AsyncWebServerRequest *req) {
        sendGzip(req, "application/manifest+json", kWebManifestGz, kWebManifestGzLen);
    });

    // REST: reads.
    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/entities", HTTP_GET, handleGetEntities);
    server.on("/api/device", HTTP_GET, handleGetDevice);
    server.on("/api/layout", HTTP_GET, handleGetLayout);

    // REST: writes (JSON bodies, buffered + size-capped by the handler).
    auto *haH = new AsyncCallbackJsonWebHandler("/api/config/ha", handlePostHa);
    haH->setMaxContentLength(kMaxBodyBytes);
    server.addHandler(haH);
    auto *layoutH = new AsyncCallbackJsonWebHandler("/api/layout", handlePostLayout);
    layoutH->setMaxContentLength(kMaxBodyBytes);
    server.addHandler(layoutH);
    auto *authH = new AsyncCallbackJsonWebHandler("/api/auth", handlePostAuth);
    authH->setMaxContentLength(kMaxBodyBytes);
    server.addHandler(authH);
    auto *dispH = new AsyncCallbackJsonWebHandler("/api/config/display", handlePostDisplay);
    dispH->setMaxContentLength(kMaxBodyBytes);
    server.addHandler(dispH);

    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        callbacks.onFactoryReset();
        req->send(200, "application/json", "{\"ok\":true}");
        armReboot(1000);
    });

    // OTA.
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!requireAuth(req)) return;
        req->send(200, "text/html", renderUpdatePage());
    });
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);

    server.onNotFound([](AsyncWebServerRequest *req) { req->redirect("/"); });
    server.begin();
    Serial.println("[portal] config server started on :80");
}

void configPortalLoop() {
    if (rebootArmed && (int32_t)(millis() - rebootDeadline) >= 0) {
        Serial.println("[app] rebooting now");
        ESP.restart();
    }
}

} // namespace net
