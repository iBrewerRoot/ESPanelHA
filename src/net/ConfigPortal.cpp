#include "ConfigPortal.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

namespace net {

namespace {

AsyncWebServer server(80);
ConfigPortalCallbacks callbacks;

// Deferred reboot after a firmware upload — set from the OTA request handler,
// fired from configPortalLoop() so the HTTP response flushes before we restart.
bool rebootArmed = false;
uint32_t rebootDeadline = 0;

String htmlEscape(const String &in) {
    String out;
    out.reserve(in.length());
    for (char c : in) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

bool isSelected(const std::vector<core::SelectedEntity> &sel, const String &id) {
    for (const auto &s : sel) {
        if (s.entityId == id) return true;
    }
    return false;
}

String renderHaForm() {
    core::HAConfig ha = callbacks.currentHa();
    String f = F("<form method='POST' action='/save_ha'><h3>Home Assistant</h3>");
    f += "<p>Host / IP</p><input name='host' value='" + htmlEscape(ha.host) + "'>";
    f += "<p>Port</p><input name='port' value='" + String(ha.port) + "'>";
    // The token is never echoed back (it would be readable in the page/source).
    // Empty field on save means "keep the stored token".
    const bool hasToken = ha.token.length() > 0;
    f += "<p>Long-lived token</p><input name='token' type='password' autocomplete='off' placeholder='";
    f += hasToken ? "Saved \xE2\x80\x94 leave blank to keep" : "Paste your token";
    f += "'>";
    if (hasToken) {
        f += F("<p>A token is already saved. Leave this blank to keep it, or paste "
               "a new one to replace it.</p>");
    }
    f += "<label class='chk'><input type='checkbox' name='tls'";
    f += ha.useTls ? " checked" : "";
    f += "> Use HTTPS/WSS (TLS)</label>";
    f += F("<button type='submit'>Save Home Assistant settings</button></form>");
    return f;
}

String renderEntities() {
    auto available = callbacks.listAvailable();
    auto selected = callbacks.currentSelection();

    String s = F("<form method='POST' action='/save'><h3>Entities to display</h3>");
    if (available.empty()) {
        s += F("<p class='note'>No entities yet. Save valid Home Assistant "
               "settings above, wait for the connection, then refresh.</p>");
    }
    for (const auto &e : available) {
        const bool checked = isSelected(selected, e.entityId);
        s += "<label class='chk'><input type='checkbox' name='e' value='";
        s += htmlEscape(e.entityId);
        s += "'";
        s += checked ? " checked" : "";
        s += "><span>";
        s += htmlEscape(e.friendlyName.length() ? e.friendlyName : e.entityId);
        s += "</span><span class='dom'>";
        s += htmlEscape(e.domain);
        s += "</span></label>";
    }
    s += F("<button type='submit'>Save selection</button></form>");
    return s;
}

// Shared <head> + page header so every portal page looks the same.
String commonHead() {
    return F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>HA Panel</title><style>"
        "body{font-family:sans-serif;margin:0;background:#111;color:#eee}"
        "header{padding:16px;background:#1e88e5;color:#fff;font-size:18px}"
        "h3{padding:0 16px;margin:18px 0 6px}"
        "form{padding:0 16px 8px}"
        "input[type=text],input[type=file],input:not([type]){width:100%;padding:10px;"
        "margin:2px 0;box-sizing:border-box;background:#1b2128;color:#eee;"
        "border:1px solid #333;border-radius:6px;font-size:16px}"
        "p{margin:8px 0 2px;color:#9aa7b2;font-size:14px}"
        ".note{color:#e0a030}"
        "a{color:#1e88e5}"
        "label.chk{display:flex;align-items:center;gap:10px;padding:10px;"
        "border-bottom:1px solid #333}"
        "label.chk input[type=checkbox]{width:22px;height:22px}"
        ".dom{color:#888;font-size:12px;margin-left:auto}"
        "button{margin-top:12px;padding:14px 20px;font-size:16px;border:0;"
        "border-radius:8px;background:#1e88e5;color:#fff;width:100%}"
        ".bar{height:14px;background:#1b2128;border:1px solid #333;border-radius:7px;"
        "overflow:hidden;margin-top:12px}"
        ".bar>div{height:100%;width:0;background:#1e88e5;transition:width .2s}"
        "</style></head><body><header>HA Control Panel setup</header>");
}

String renderPage() {
    String page = commonHead();
    page += renderHaForm();
    page += renderEntities();
    page += F("<form><h3>Firmware</h3>"
              "<p><a href='/update'>Update firmware (OTA)</a></p></form>");
    page += F("</body></html>");
    return page;
}

// Browser OTA page: streams the .bin to /update with a live progress bar.
String renderUpdatePage() {
    String page = commonHead();
    page += F(
        "<form id='f'><h3>Firmware update</h3>"
        "<p>Select a firmware .bin built for this board, then upload. The panel "
        "reboots automatically when done. Do not power off during the update.</p>"
        "<input type='file' id='file' accept='.bin'>"
        "<button type='submit'>Upload &amp; flash</button>"
        "<div class='bar'><div id='fill'></div></div>"
        "<p id='msg'></p>"
        "<p><a href='/'>&larr; Back</a></p></form>"
        "<script>"
        "var f=document.getElementById('f');"
        "f.onsubmit=function(e){e.preventDefault();"
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
    return page;
}

void handleSaveHa(AsyncWebServerRequest *req) {
    core::HAConfig ha = callbacks.currentHa();  // preserve fields not re-entered (token)
    if (req->hasParam("host", true)) ha.host = req->getParam("host", true)->value();
    if (req->hasParam("port", true)) {
        ha.port = static_cast<uint16_t>(req->getParam("port", true)->value().toInt());
    }
    if (ha.port == 0) ha.port = 8123;
    // Only overwrite the token when a new one is entered; blank keeps the stored one.
    if (req->hasParam("token", true)) {
        String tok = req->getParam("token", true)->value();
        tok.trim();
        if (tok.length()) ha.token = tok;
    }
    ha.useTls = req->hasParam("tls", true);  // checkbox only present when checked
    callbacks.onSaveHa(ha);
    req->redirect("/");
}

void handleSave(AsyncWebServerRequest *req) {
    std::vector<core::SelectedEntity> selection;
    const size_t count = req->params();
    for (size_t i = 0; i < count; i++) {
        const AsyncWebParameter *p = req->getParam(i);
        if (p->isPost() && p->name() == "e") {
            core::SelectedEntity se;
            se.entityId = p->value();
            se.label = "";  // label resolved from HA friendly_name on apply
            selection.push_back(se);
        }
    }
    callbacks.onSave(selection);
    req->redirect("/");
}

// Streams an uploaded firmware image into the OTA flash partition chunk by chunk.
void handleUpdateUpload(AsyncWebServerRequest *req, const String &filename,
                        size_t index, uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        Serial.printf("[ota] start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }
    }
    if (Update.isRunning() && len) {
        if (Update.write(data, len) != len) Update.printError(Serial);
    }
    if (final) {
        if (Update.end(true)) {
            Serial.printf("[ota] success: %u bytes\n", (unsigned)(index + len));
        } else {
            Update.printError(Serial);
        }
    }
}

// Sent after the upload completes; reports the result and arms the reboot.
void handleUpdateDone(AsyncWebServerRequest *req) {
    const bool ok = !Update.hasError();
    AsyncWebServerResponse *res = req->beginResponse(
        ok ? 200 : 500, "text/plain",
        ok ? "Update OK — rebooting..." : "Update failed. Check the .bin and retry.");
    res->addHeader("Connection", "close");
    req->send(res);
    if (ok) {
        rebootDeadline = millis() + 1500;  // let the response flush first
        rebootArmed = true;
    }
}

} // namespace

void configPortalBegin(const ConfigPortalCallbacks &cb) {
    callbacks = cb;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", renderPage());
    });
    server.on("/save_ha", HTTP_POST, handleSaveHa);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", renderUpdatePage());
    });
    server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });
    server.begin();
    Serial.println("[portal] config server started on :80");
}

void configPortalLoop() {
    if (rebootArmed && (int32_t)(millis() - rebootDeadline) >= 0) {
        Serial.println("[ota] rebooting now");
        ESP.restart();
    }
}

} // namespace net
