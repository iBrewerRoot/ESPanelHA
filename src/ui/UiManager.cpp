#include "UiManager.h"

#include <lvgl.h>
#include <map>

namespace ui {

namespace {

Actions actions;

// Screens.
lv_obj_t *screenBoot = nullptr;
lv_obj_t *screenSetup = nullptr;
lv_obj_t *screenInfo = nullptr;
lv_obj_t *screenDash = nullptr;

lv_obj_t *bootLabel = nullptr;
lv_obj_t *setupLabel = nullptr;
lv_obj_t *setupApLabel = nullptr;
lv_obj_t *infoBodyLabel = nullptr;
lv_obj_t *infoUrlLabel = nullptr;
lv_obj_t *dashGrid = nullptr;
lv_obj_t *connLabel = nullptr;

/* Widgets for one dashboard card, keyed by entity id. */
struct Card {
    lv_obj_t *sw = nullptr;      // on/off toggle
    lv_obj_t *slider = nullptr;  // brightness (lights only), may be null
    String *idHolder = nullptr;  // heap-owned id passed as widget user_data
};
std::map<String, Card> cards;

// Guard so programmatic widget updates don't fire the user-action callbacks.
bool suppressEvents = false;

lv_obj_t *makeScreen() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, lv_color_hex(0x101418), 0);
    return s;
}

void onSwitchEvent(lv_event_t *e) {
    if (suppressEvents) return;
    auto *id = static_cast<String *>(lv_event_get_user_data(e));
    if (id && actions.onToggle) actions.onToggle(*id);
}

void onSliderEvent(lv_event_t *e) {
    if (suppressEvents) return;
    auto *id = static_cast<String *>(lv_event_get_user_data(e));
    auto *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (id && actions.onBrightness) {
        actions.onBrightness(*id, static_cast<int>(lv_slider_get_value(slider)));
    }
}

void buildCard(const core::SelectedEntity &sel, const ha::EntityState *state) {
    Card card;
    card.idHolder = new String(sel.entityId);

    lv_obj_t *cont = lv_obj_create(dashGrid);
    lv_obj_set_size(cont, lv_pct(46), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1b2128), 0);
    lv_obj_set_style_radius(cont, 14, 0);
    lv_obj_set_style_pad_all(cont, 14, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(cont, 12, 0);

    lv_obj_t *name = lv_label_create(cont);
    const String label = sel.label.length() ? sel.label
                         : (state ? state->friendlyName : sel.entityId);
    lv_label_set_text(name, label.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_color(name, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_24, 0);

    card.sw = lv_switch_create(cont);
    lv_obj_set_size(card.sw, 80, 44);  // large enough for touch on a dense panel
    lv_obj_add_event_cb(card.sw, onSwitchEvent, LV_EVENT_VALUE_CHANGED, card.idHolder);

    const String domain = ha::EntityStore::domainOf(sel.entityId);
    if (domain == "light") {
        card.slider = lv_slider_create(cont);
        lv_slider_set_range(card.slider, 0, 100);
        lv_obj_set_width(card.slider, lv_pct(100));
        lv_obj_set_height(card.slider, 18);
        lv_obj_add_event_cb(card.slider, onSliderEvent, LV_EVENT_RELEASED, card.idHolder);
    }

    cards[sel.entityId] = card;

    if (state) uiUpdateEntity(*state);
}

void clearDashboard() {
    for (auto &kv : cards) {
        delete kv.second.idHolder;
    }
    cards.clear();
    if (dashGrid) lv_obj_clean(dashGrid);
}

} // namespace

void uiInit() {
    screenBoot = makeScreen();
    bootLabel = lv_label_create(screenBoot);
    lv_obj_center(bootLabel);
    lv_obj_set_style_text_color(bootLabel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(bootLabel, &lv_font_montserrat_40, 0);
    lv_label_set_text(bootLabel, "Starting...");

    // Setup screen: a full-height column so content spreads over the whole panel.
    screenSetup = makeScreen();
    lv_obj_set_style_pad_all(screenSetup, 22, 0);
    lv_obj_set_flex_flow(screenSetup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screenSetup, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *setupTitle = lv_label_create(screenSetup);
    lv_label_set_text(setupTitle, "Setup required");
    lv_obj_set_style_text_color(setupTitle, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(setupTitle, &lv_font_montserrat_40, 0);

    setupLabel = lv_label_create(screenSetup);
    lv_obj_set_width(setupLabel, lv_pct(100));
    lv_label_set_long_mode(setupLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(setupLabel, lv_color_hex(0xc8d2db), 0);
    lv_obj_set_style_text_font(setupLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(setupLabel, LV_TEXT_ALIGN_CENTER, 0);

    setupApLabel = lv_label_create(screenSetup);
    lv_obj_set_width(setupApLabel, lv_pct(100));
    lv_label_set_long_mode(setupApLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(setupApLabel, lv_color_hex(0x1e88e5), 0);
    lv_obj_set_style_text_font(setupApLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(setupApLabel, LV_TEXT_ALIGN_CENTER, 0);

    // Info screen: shown after connecting, points the user to the web portal.
    screenInfo = makeScreen();
    lv_obj_set_style_pad_all(screenInfo, 22, 0);
    lv_obj_set_flex_flow(screenInfo, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screenInfo, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *infoTitle = lv_label_create(screenInfo);
    lv_label_set_text(infoTitle, "Connected");
    lv_obj_set_style_text_color(infoTitle, lv_color_hex(0x4caf50), 0);
    lv_obj_set_style_text_font(infoTitle, &lv_font_montserrat_40, 0);

    infoBodyLabel = lv_label_create(screenInfo);
    lv_obj_set_width(infoBodyLabel, lv_pct(100));
    lv_label_set_long_mode(infoBodyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(infoBodyLabel, lv_color_hex(0xc8d2db), 0);
    lv_obj_set_style_text_font(infoBodyLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(infoBodyLabel, LV_TEXT_ALIGN_CENTER, 0);

    infoUrlLabel = lv_label_create(screenInfo);
    lv_obj_set_width(infoUrlLabel, lv_pct(100));
    lv_label_set_long_mode(infoUrlLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(infoUrlLabel, lv_color_hex(0x1e88e5), 0);
    lv_obj_set_style_text_font(infoUrlLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(infoUrlLabel, LV_TEXT_ALIGN_CENTER, 0);

    screenDash = makeScreen();
    connLabel = lv_label_create(screenDash);
    lv_obj_align(connLabel, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_set_style_text_color(connLabel, lv_color_hex(0x8aa0b2), 0);
    lv_obj_set_style_text_font(connLabel, &lv_font_montserrat_18, 0);
    lv_label_set_text(connLabel, "");

    dashGrid = lv_obj_create(screenDash);
    lv_obj_set_size(dashGrid, lv_pct(100), lv_pct(92));
    lv_obj_align(dashGrid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(dashGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dashGrid, 0, 0);
    // Responsive: wrap cards across rows; spacing adapts to any screen size.
    lv_obj_set_flex_flow(dashGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(dashGrid, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_scr_load(screenBoot);
}

void uiSetActions(const Actions &a) { actions = a; }

void uiShowBoot(const String &message) {
    lv_label_set_text(bootLabel, message.c_str());
    lv_scr_load(screenBoot);
}

void uiShowSetup(const String &apName) {
    lv_label_set_text(setupLabel,
                      "Connect your phone to this WiFi network, then open the "
                      "captive portal to enter your WiFi and Home Assistant "
                      "details.");
    lv_label_set_text(setupApLabel, apName.c_str());
    lv_scr_load(screenSetup);
}

void uiShowConfigureHa(const String &ip) {
    lv_label_set_text(infoBodyLabel,
                      "WiFi connected. Open this address in your browser to set "
                      "up your Home Assistant connection:");
    String url = "http://" + ip + "/";
    lv_label_set_text(infoUrlLabel, url.c_str());
    lv_scr_load(screenInfo);
}

void uiShowConnected(const String &ip) {
    lv_label_set_text(infoBodyLabel,
                      "Open this address in your browser to choose which "
                      "entities to display:");
    String url = "http://" + ip + "/";
    lv_label_set_text(infoUrlLabel, url.c_str());
    lv_scr_load(screenInfo);
}

void uiShowDashboard(const std::vector<core::SelectedEntity> &entities,
                     const ha::EntityStore &store) {
    clearDashboard();
    for (const auto &sel : entities) {
        buildCard(sel, store.get(sel.entityId));
    }
    lv_scr_load(screenDash);
}

void uiUpdateEntity(const ha::EntityState &e) {
    auto it = cards.find(e.entityId);
    if (it == cards.end()) return;
    Card &card = it->second;

    suppressEvents = true;
    const bool on = (e.state == "on");
    if (on) lv_obj_add_state(card.sw, LV_STATE_CHECKED);
    else lv_obj_clear_state(card.sw, LV_STATE_CHECKED);

    if (card.slider) {
        const int pct = e.brightness >= 0 ? (e.brightness * 100 + 127) / 255 : (on ? 100 : 0);
        lv_slider_set_value(card.slider, pct, LV_ANIM_OFF);
    }
    suppressEvents = false;
}

void uiSetConnectionStatus(const String &text) {
    if (connLabel) lv_label_set_text(connLabel, text.c_str());
}

} // namespace ui
