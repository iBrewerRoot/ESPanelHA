#include "UiManager.h"

#include "mdi_icons.h"

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

/* Palette — kept close to Home Assistant's own tile-card look. */
constexpr uint32_t kScreenBg   = 0x101418;
constexpr uint32_t kTileBg     = 0x1b2128;  // tile, inactive
constexpr uint32_t kIconOff    = 0x5b6571;  // muted icon when off / idle
constexpr uint32_t kAmber      = 0xFFC107;  // lights on (no color info)
constexpr uint32_t kPrimary    = 0x03A9F4;  // HA primary blue (switches, sensors)
constexpr uint32_t kTextMuted  = 0x8aa0b2;

/* Widgets for one dashboard tile, keyed by entity id. */
struct Card {
    lv_obj_t *tile = nullptr;
    lv_obj_t *icon = nullptr;    // MDI glyph, recolored with state
    lv_obj_t *stateLbl = nullptr;
    lv_obj_t *slider = nullptr;  // brightness (lights only), may be null
    String *idHolder = nullptr;  // heap-owned id passed as widget user_data
    String domain;
};
std::map<String, Card> cards;

// Guard so programmatic widget updates don't fire the user-action callbacks.
bool suppressEvents = false;

lv_obj_t *makeScreen() {
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, lv_color_hex(kScreenBg), 0);
    return s;
}

// "Active" in the HA sense — drives the accent coloring of the tile.
bool isActive(const ha::EntityState &e) {
    return e.state == "on" || e.state == "open" || e.state == "home" ||
           e.state == "playing" || e.state == "unlocked";
}

// Icon glyph: prefer HA's own attributes.icon, then a domain/device-class
// default, so a tile always shows something meaningful.
const char *resolveGlyph(const String &domain, const ha::EntityState *st) {
    if (st && st->icon.length()) {
        if (const char *g = mdiGlyph(st->icon.c_str())) return g;
    }
    if (domain == "light") return MDI_LIGHTBULB;
    if (domain == "switch") return MDI_POWER_SOCKET_EU;
    if (domain == "sensor") {
        const String dc = st ? st->deviceClass : String();
        if (dc == "temperature") return MDI_THERMOMETER;
        if (dc == "humidity") return MDI_WATER_PERCENT;
        if (dc == "power" || dc == "energy") return MDI_LIGHTNING_BOLT;
        return MDI_GAUGE;
    }
    return MDI_HELP_CIRCLE_OUTLINE;
}

// Accent color for the current state — mirrors the bulb's real RGB when known.
lv_color_t accentFor(const String &domain, const ha::EntityState &e, bool active) {
    if (domain == "sensor") return lv_color_hex(kPrimary);  // sensors: always lit
    if (!active) return lv_color_hex(kIconOff);
    if (domain == "light") {
        return e.rgb >= 0 ? lv_color_hex((uint32_t)e.rgb) : lv_color_hex(kAmber);
    }
    return lv_color_hex(kPrimary);
}

int brightnessPct(const ha::EntityState &e, bool active) {
    if (e.brightness >= 0) return (e.brightness * 100 + 127) / 255;
    return active ? 100 : 0;
}

// Human-readable state line, HA-style ("On · 60%", "21.5 °C").
String stateText(const String &domain, const ha::EntityState &e, bool active) {
    if (domain == "light") {
        if (!active) return "Off";
        String s = "On";
        if (e.brightness >= 0) { s += " \xC2\xB7 "; s += brightnessPct(e, true); s += "%"; }
        return s;
    }
    if (domain == "switch") return active ? "On" : "Off";
    if (domain == "sensor") {
        String s = e.state;
        if (e.unit.length()) { s += " "; s += e.unit; }
        return s;
    }
    return e.state;
}

// Repaint a tile from an entity state (icon glyph + accent + tint + text + slider).
void applyVisual(Card &card, const ha::EntityState &e) {
    const bool active = isActive(e);
    const lv_color_t accent = accentFor(card.domain, e, active);
    const lv_color_t tileBg =
        active ? lv_color_mix(accent, lv_color_hex(kTileBg), 60) : lv_color_hex(kTileBg);

    lv_label_set_text(card.icon, resolveGlyph(card.domain, &e));
    lv_obj_set_style_text_color(card.icon, accent, 0);
    lv_obj_set_style_bg_color(card.tile, tileBg, 0);

    if (card.stateLbl) lv_label_set_text(card.stateLbl, stateText(card.domain, e, active).c_str());

    if (card.slider) {
        suppressEvents = true;
        lv_slider_set_value(card.slider, brightnessPct(e, active), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(card.slider, accent, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(card.slider, accent, LV_PART_KNOB);
        suppressEvents = false;
    }
}

void onTileEvent(lv_event_t *e) {
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
    card.domain = ha::EntityStore::domainOf(sel.entityId);

    lv_obj_t *tile = lv_obj_create(dashGrid);
    card.tile = tile;
    lv_obj_set_size(tile, lv_pct(46), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(tile, lv_color_hex(kTileBg), 0);
    lv_obj_set_style_radius(tile, 18, 0);
    lv_obj_set_style_pad_all(tile, 16, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tile, 8, 0);
    // Whole tile toggles (HA tile-card behaviour); press gives visual feedback.
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x232b34), LV_STATE_PRESSED);
    lv_obj_add_event_cb(tile, onTileEvent, LV_EVENT_CLICKED, card.idHolder);

    card.icon = lv_label_create(tile);
    lv_obj_set_style_text_font(card.icon, &mdi_font, 0);
    lv_obj_set_style_text_color(card.icon, lv_color_hex(kIconOff), 0);
    lv_label_set_text(card.icon, resolveGlyph(card.domain, state));

    lv_obj_t *name = lv_label_create(tile);
    const String label = sel.label.length() ? sel.label
                         : (state ? state->friendlyName : sel.entityId);
    lv_label_set_text(name, label.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_color(name, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);

    card.stateLbl = lv_label_create(tile);
    lv_label_set_text(card.stateLbl, "");
    lv_obj_set_style_text_color(card.stateLbl, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(card.stateLbl, &lv_font_montserrat_14, 0);

    if (card.domain == "light") {
        card.slider = lv_slider_create(tile);
        lv_slider_set_range(card.slider, 0, 100);
        lv_obj_set_width(card.slider, lv_pct(100));
        lv_obj_set_height(card.slider, 14);
        lv_obj_add_event_cb(card.slider, onSliderEvent, LV_EVENT_RELEASED, card.idHolder);
    }

    cards[sel.entityId] = card;

    if (state) applyVisual(cards[sel.entityId], *state);
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
    applyVisual(it->second, e);
}

void uiSetConnectionStatus(const String &text) {
    if (connLabel) lv_label_set_text(connLabel, text.c_str());
}

} // namespace ui
