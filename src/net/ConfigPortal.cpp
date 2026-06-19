#include "ConfigPortal.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

namespace net {

namespace {

AsyncWebServer server(80);
ConfigPortalCallbacks callbacks;

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
    f += "<p>Long-lived token</p><input name='token' value='" + htmlEscape(ha.token) + "'>";
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

String renderPage() {
    String page = F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>HA Panel</title><style>"
        "body{font-family:sans-serif;margin:0;background:#111;color:#eee}"
        "header{padding:16px;background:#1e88e5;color:#fff;font-size:18px}"
        "h3{padding:0 16px;margin:18px 0 6px}"
        "form{padding:0 16px 8px}"
        "input[type=text],input:not([type]){width:100%;padding:10px;margin:2px 0;"
        "box-sizing:border-box;background:#1b2128;color:#eee;border:1px solid #333;"
        "border-radius:6px;font-size:16px}"
        "p{margin:8px 0 2px;color:#9aa7b2;font-size:14px}"
        ".note{color:#e0a030}"
        "label.chk{display:flex;align-items:center;gap:10px;padding:10px;"
        "border-bottom:1px solid #333}"
        "label.chk input[type=checkbox]{width:22px;height:22px}"
        ".dom{color:#888;font-size:12px;margin-left:auto}"
        "button{margin-top:12px;padding:14px 20px;font-size:16px;border:0;"
        "border-radius:8px;background:#1e88e5;color:#fff;width:100%}"
        "</style></head><body><header>HA Control Panel setup</header>");

    page += renderHaForm();
    page += renderEntities();
    page += F("</body></html>");
    return page;
}

void handleSaveHa(AsyncWebServerRequest *req) {
    core::HAConfig ha;
    if (req->hasParam("host", true)) ha.host = req->getParam("host", true)->value();
    if (req->hasParam("port", true)) {
        ha.port = static_cast<uint16_t>(req->getParam("port", true)->value().toInt());
    }
    if (ha.port == 0) ha.port = 8123;
    if (req->hasParam("token", true)) ha.token = req->getParam("token", true)->value();
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

} // namespace

void configPortalBegin(const ConfigPortalCallbacks &cb) {
    callbacks = cb;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", renderPage());
    });
    server.on("/save_ha", HTTP_POST, handleSaveHa);
    server.on("/save", HTTP_POST, handleSave);
    server.onNotFound([](AsyncWebServerRequest *req) {
        req->redirect("/");
    });
    server.begin();
    Serial.println("[portal] config server started on :80");
}

} // namespace net
