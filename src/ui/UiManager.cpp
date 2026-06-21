#include "UiManager.h"

#include "UiTheme.h"
#include "board/BoardConfig.h"  // SCREEN_WIDTH/HEIGHT + BOARD_NAME (ui may know the board)
#include "mdi_icons.h"
#include "text_fonts.h"  // accented Montserrat (entity names with é, è, à, ç, …)

#include <lvgl.h>
#include <map>
#include <vector>

namespace ui {

// Single source of truth for dashboard geometry + style. The on-screen renderer
// below reads from this, and the config portal serves the same values as JSON,
// so the browser preview stays pixel-faithful to the device.
const DashboardSpec &dashboardSpec() {
    static const DashboardSpec spec = {
        /* boardName */         BOARD_NAME,
        /* screenW/H */         SCREEN_WIDTH, SCREEN_HEIGHT,
        /* gridCols */          2,
        /* rowHeightPx */       104,
        /* colGapPx, rowGapPx */ 10, 10,
        /* pagePadPx */         10,
        /* pageTitleGapPx */    8,
        /* topBarHeightPx */    42,   // ~= status icon height
        /* dotsBarHeightPx */   20,   // 9px dots lifted ~9px off the bottom edge
        /* tileRadiusPx */      18,
        /* tilePadPx */         16,
        /* tileGapPx */         8,
        /* iconSizePx */        40,
        /* nameFontPx */        18,
        /* stateFontPx */       14,
        /* titleFontPx */       24,
        /* sliderHeightPx */    14,
        /* valueFontSmallPx */  24,
        /* valueFontMedPx */    32,
        /* valueFontLargePx */  40,
        /* screenBg */          0x101418,
        /* tileBg */            0x1b2128,
        /* pressedBg */         0x232b34,
        /* iconOff */           0x5b6571,
        /* amber */             0xFFC107,
        /* primary */           0x03A9F4,
        /* textMuted */         0x8aa0b2,
        /* nameColor */         0xffffff,
        /* activeMix */         60,
    };
    return spec;
}

namespace {

const DashboardSpec &S = dashboardSpec();

Actions actions;

// Screens.
lv_obj_t *screenBoot = nullptr;
lv_obj_t *screenSetup = nullptr;
lv_obj_t *screenInfo = nullptr;
lv_obj_t *screenDash = nullptr;
lv_obj_t *screenSettings = nullptr;

lv_obj_t *bootLabel = nullptr;
lv_obj_t *setupLabel = nullptr;
lv_obj_t *setupApLabel = nullptr;
lv_obj_t *infoBodyLabel = nullptr;
lv_obj_t *infoUrlLabel = nullptr;

// Settings / info screen (config-portal URL + future controls).
lv_obj_t *settingsUrlLabel = nullptr;
lv_obj_t *settingsHint = nullptr;
String settingsUrl;

// Phone-style settings menu, reached by swiping down on the dashboard.
lv_obj_t *screenMenu = nullptr;
lv_obj_t *menuWifiIcon = nullptr;   // WiFi-to-AP signal indicator (top-left)
lv_obj_t *menuConnIcon = nullptr;   // HA status badge (kept in sync with connIcon)
lv_obj_t *menuPanel = nullptr;      // lighter surface for future setting buttons
lv_obj_t *menuIpLabel = nullptr;    // portal URL, at the bottom

// Dashboard top/bottom chrome (persistent across page swipes).
lv_obj_t *connIcon = nullptr;             // HA status badge, top-right corner
lv_obj_t *titleLabel = nullptr;           // centered page title, same line as icon
lv_obj_t *dotsCont = nullptr;             // page-indicator dots container
std::vector<lv_obj_t *> pageDots;

// Multi-page dashboard (lazy: only the visible page's widgets exist).
lv_obj_t *tileview = nullptr;
std::vector<lv_obj_t *> pageTiles;        // one tileview tile per page
int currentPage = -1;
core::Layout layout;                      // copy kept to build pages on demand
const ha::EntityStore *entityStore = nullptr;
std::vector<lv_coord_t> rowDsc;           // grid row template for the live page

/* Geometry + palette: aliased from the shared DashboardSpec so the device and
 * the web preview can never drift (single source of truth in dashboardSpec()). */
const lv_coord_t kRowH      = S.rowHeightPx;  // px per grid row (h=2 spans two)
const uint32_t kScreenBg    = S.screenBg;
const uint32_t kTileBg      = S.tileBg;     // tile, inactive
const uint32_t kIconOff     = S.iconOff;    // muted icon when off / idle
const uint32_t kAmber       = S.amber;      // lights on (no color info)
const uint32_t kPrimary     = S.primary;    // HA primary blue (switches, sensors)
const uint32_t kTextMuted   = S.textMuted;

// Connection-status badge colors (device chrome; not part of tile geometry).
const uint32_t kStatusOk    = 0x4CAF50;     // connected (Ready)
const uint32_t kStatusDown  = 0xF44336;     // disconnected / auth failed

// Map a pixel size to the matching font pointer (the spec carries the size; the
// device needs the concrete font). Uses our accented Montserrat subsets so
// entity names with French accents render correctly.
const lv_font_t *fontForPx(int px) {
    switch (px) {
        case 14: return &montserrat_fr_14;
        case 18: return &montserrat_fr_18;
        case 24: return &montserrat_fr_24;
        case 28: return &montserrat_fr_28;
        case 32: return &montserrat_fr_32;
        case 40: return &montserrat_fr_40;
        default: return &montserrat_fr_18;
    }
}

// Content width (px) of a tile spanning `w` grid cells, derived from the spec
// geometry (used to decide how a wrapped name fits).
int tileContentWidth(int w) {
    const int cellW =
        (S.screenW - 2 * S.pagePadPx - (S.gridCols - 1) * S.colGapPx) / S.gridCols;
    return w * cellW + (w - 1) * S.colGapPx - 2 * S.tilePadPx;
}

// Largest font (<= basePx) that wraps `text` within widthPx into a block no
// taller than maxHeightPx, so a long name shrinks instead of overflowing.
const lv_font_t *nameFontFor(const String &text, int widthPx, int basePx, int maxHeightPx) {
    const int candidates[] = {basePx, 14};
    for (int px : candidates) {
        if (px > basePx) continue;
        const lv_font_t *f = fontForPx(px);
        lv_point_t sz;
        lv_txt_get_size(&sz, text.c_str(), f, 0, 0, widthPx, LV_TEXT_FLAG_NONE);
        if (sz.y <= maxHeightPx) return f;
    }
    return fontForPx(14);
}

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

// Sensors are display-only; light/switch react to a tap.
bool isControllable(const String &domain) {
    return domain == "light" || domain == "switch";
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
    return lv_color_hex(kAmber);  // switches & other on/off entities: amber when ON
}

// Tile-background tint when active. Keeps the calmer pre-amber palette (switches
// primary, lights their own color) so the active fill isn't as warm as the icon.
lv_color_t tileTintFor(const String &domain, const ha::EntityState &e) {
    if (domain == "light") {
        return e.rgb >= 0 ? lv_color_hex((uint32_t)e.rgb) : lv_color_hex(kAmber);
    }
    return lv_color_hex(kPrimary);  // switches & co (only used while active)
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
    const lv_color_t accent = accentFor(card.domain, e, active);  // icon + slider
    const lv_color_t tileBg =
        active ? lv_color_mix(tileTintFor(card.domain, e), lv_color_hex(kTileBg), 60)
               : lv_color_hex(kTileBg);

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

// Where a tile sits on the 2-column grid.
struct Placement {
    uint8_t col, row, w, h;
};

// First-fit packing of variable-span tiles onto a 2-column grid. Returns the
// total number of rows used; fills `out` with one placement per tile (in order).
int packTiles(const std::vector<core::LayoutTile> &tiles, std::vector<Placement> &out) {
    std::vector<std::vector<bool>> occ;  // occ[row][col], col in {0,1}
    auto isFree = [&](int r, int c, int w, int h) {
        for (int dr = 0; dr < h; dr++) {
            for (int dc = 0; dc < w; dc++) {
                const int rr = r + dr, cc = c + dc;
                if (cc >= 2) return false;
                if (rr < (int)occ.size() && occ[rr][cc]) return false;
            }
        }
        return true;
    };
    for (const auto &t : tiles) {
        const int w = t.w < 1 ? 1 : (t.w > 2 ? 2 : t.w);
        const int h = t.h < 1 ? 1 : (t.h > 2 ? 2 : t.h);
        int pr = 0, pc = 0;
        bool placed = false;
        for (int r = 0; !placed && r < 256; r++) {
            for (int c = 0; c + w <= 2; c++) {
                if (isFree(r, c, w, h)) { pr = r; pc = c; placed = true; break; }
            }
        }
        while ((int)occ.size() < pr + h) occ.push_back({false, false});
        for (int dr = 0; dr < h; dr++) {
            for (int dc = 0; dc < w; dc++) occ[pr + dr][pc + dc] = true;
        }
        out.push_back({(uint8_t)pc, (uint8_t)pr, (uint8_t)w, (uint8_t)h});
    }
    return (int)occ.size();
}

// Build one entity tile inside the page's grid container.
void buildTile(lv_obj_t *parent, const core::LayoutTile &t, const Placement &p) {
    Card card;
    card.idHolder = new String(t.entityId);
    card.domain = ha::EntityStore::domainOf(t.entityId);
    const ha::EntityState *state = entityStore ? entityStore->get(t.entityId) : nullptr;

    lv_obj_t *tile = lv_obj_create(parent);
    card.tile = tile;
    lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, p.col, p.w,
                         LV_GRID_ALIGN_STRETCH, p.row, p.h);
    lv_obj_set_style_bg_color(tile, lv_color_hex(kTileBg), 0);
    lv_obj_set_style_radius(tile, S.tileRadiusPx, 0);
    lv_obj_set_style_pad_all(tile, S.tilePadPx, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(tile, S.tileGapPx, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    // Whole tile toggles (HA tile-card behaviour); sensors are display-only.
    if (isControllable(card.domain)) {
        lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(tile, lv_color_hex(S.pressedBg), LV_STATE_PRESSED);
        lv_obj_add_event_cb(tile, onTileEvent, LV_EVENT_CLICKED, card.idHolder);
    }

    // Entity icon pinned to the top-left corner, inset by the tile padding so it
    // clears the rounded corner and stays at the same spot for any tile height.
    card.icon = lv_label_create(tile);
    lv_obj_add_flag(card.icon, LV_OBJ_FLAG_IGNORE_LAYOUT);  // out of the flex flow
    lv_obj_set_style_text_font(card.icon, &mdi_font, 0);
    lv_obj_set_style_text_color(card.icon, lv_color_hex(kIconOff), 0);
    lv_label_set_text(card.icon, resolveGlyph(card.domain, state));
    lv_obj_align(card.icon, LV_ALIGN_TOP_LEFT, 0, 0);

    const int valuePx = valueFontFor(S, p.w, p.h);
    const int contentW = tileContentWidth(p.w);
    const int contentH = p.h * S.rowHeightPx + (p.h - 1) * S.rowGapPx - 2 * S.tilePadPx;

    if (p.h == 1) {
        // Short tile: value sits on the icon's row, pushed to the right (so it's
        // at the icon's level); the name goes below, clear of the icon.
        lv_obj_t *topRow = lv_obj_create(tile);
        lv_obj_remove_style_all(topRow);
        lv_obj_set_width(topRow, lv_pct(100));
        lv_obj_set_height(topRow, S.iconSizePx);
        lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(topRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

        card.stateLbl = lv_label_create(topRow);
        lv_obj_set_width(card.stateLbl, contentW - S.iconSizePx - S.tileGapPx);
        lv_label_set_long_mode(card.stateLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(card.stateLbl, LV_TEXT_ALIGN_RIGHT, 0);
    } else {
        // Tall tile: value centered on the whole tile.
        lv_obj_t *valueWrap = lv_obj_create(tile);
        lv_obj_remove_style_all(valueWrap);  // transparent, no padding/border
        lv_obj_set_width(valueWrap, lv_pct(100));
        lv_obj_set_flex_grow(valueWrap, 1);  // fill the space above the name
        lv_obj_set_flex_flow(valueWrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(valueWrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(valueWrap, LV_OBJ_FLAG_SCROLLABLE);

        card.stateLbl = lv_label_create(valueWrap);
        lv_obj_set_flex_grow(card.stateLbl, 1);
        lv_label_set_long_mode(card.stateLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(card.stateLbl, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_label_set_text(card.stateLbl, "");
    lv_obj_set_style_text_color(card.stateLbl, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(card.stateLbl, fontForPx(valuePx), 0);

    // Entity name at the bottom. On short (h=1) tiles it lives in the area below
    // the icon row (so it can't overlap the icon) and the font shrinks to fit;
    // tall tiles keep the base size with two-line wrapping.
    lv_obj_t *name = lv_label_create(tile);
    const String label = t.label.length() ? t.label
                         : (state ? state->friendlyName : t.entityId);
    lv_label_set_text(name, label.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_color(name, lv_color_hex(S.nameColor), 0);
    const int twoLines = lv_font_get_line_height(fontForPx(S.nameFontPx)) * 2;
    const int nameMaxH = p.h == 1 ? (contentH - S.iconSizePx) : twoLines;
    lv_obj_set_style_text_font(name, nameFontFor(label, contentW, S.nameFontPx, nameMaxH), 0);

    // Light brightness slider as a thin footer under the name.
    if (card.domain == "light") {
        card.slider = lv_slider_create(tile);
        lv_slider_set_range(card.slider, 0, 100);
        lv_obj_set_width(card.slider, lv_pct(100));
        lv_obj_set_height(card.slider, S.sliderHeightPx);
        lv_obj_add_event_cb(card.slider, onSliderEvent, LV_EVENT_RELEASED, card.idHolder);
    }

    cards[t.entityId] = card;
    if (state) applyVisual(cards[t.entityId], *state);
}

// Repaint the persistent chrome (title text + active dot) for the live page.
void updateChrome() {
    if (titleLabel && currentPage >= 0 && currentPage < (int)layout.pages.size()) {
        lv_label_set_text(titleLabel, layout.pages[currentPage].title.c_str());
    }
    for (size_t i = 0; i < pageDots.size(); i++) {
        const bool on = (int)i == currentPage;
        // Fully opaque (no LV_OPA_50): a semi-transparent fill re-blends over the
        // partial buffer on every swipe and the dots visibly accumulate/degrade.
        lv_obj_set_style_bg_color(pageDots[i],
            lv_color_hex(on ? kPrimary : kIconOff), 0);
        lv_obj_set_style_bg_opa(pageDots[i], LV_OPA_COVER, 0);
    }
    if (dotsCont) lv_obj_invalidate(dotsCont);  // clean repaint after a page change
}

// Force a clean full repaint of the page area while scrolling. The partial
// render buffer can otherwise leave horizontal streaks behind moving content
// (only cleared on the next page swipe); invalidating the tileview each scroll
// step repaints the whole visible area.
void onContentScroll(lv_event_t *) {
    if (tileview) lv_obj_invalidate(tileview);
}

// While the tileview animates between pages, repaint the bottom dots each frame
// so the partial render buffer doesn't smear them (they otherwise only reset on
// a full-screen reload, e.g. opening the settings menu).
void onTileviewScroll(lv_event_t *) {
    if (dotsCont) lv_obj_invalidate(dotsCont);
}

// Realize one page's grid of tiles into its tileview tile.
void buildPage(int idx) {
    if (idx < 0 || idx >= (int)layout.pages.size()) return;
    lv_obj_t *pageTile = pageTiles[idx];
    const core::LayoutPage &page = layout.pages[idx];

    lv_obj_t *content = lv_obj_create(pageTile);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);     // vertical scroll if it overflows
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);  // dots/gesture only
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLL_ELASTIC);  // so a top pull-down is a gesture
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLL_CHAIN);  // horizontal swipe -> tileview
    lv_obj_add_event_cb(content, onContentScroll, LV_EVENT_SCROLL, nullptr);

    std::vector<Placement> place;
    int rows = packTiles(page.tiles, place);
    if (rows < 1) rows = 1;

    // Center the content when it doesn't use the full grid: a lone tile centers
    // on the screen, and a layout that only fills the left column centers that
    // column. Otherwise the two columns stretch to fill the width as usual.
    bool col1Used = false;
    for (const auto &pl : place) if (pl.col + pl.w > 1) col1Used = true;
    const bool singleColumn = !place.empty() && !col1Used;
    const bool singleTile = place.size() == 1;
    const lv_coord_t cellW =
        (S.screenW - 2 * S.pagePadPx - S.colGapPx) / S.gridCols;  // one half-width cell

    static lv_coord_t colFull[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t colOne[] = {0, LV_GRID_TEMPLATE_LAST};
    colOne[0] = cellW;
    rowDsc.clear();
    for (int i = 0; i < rows; i++) rowDsc.push_back(kRowH);
    rowDsc.push_back(LV_GRID_TEMPLATE_LAST);

    lv_obj_set_grid_dsc_array(content, singleColumn ? colOne : colFull, rowDsc.data());
    lv_obj_set_layout(content, LV_LAYOUT_GRID);
    lv_obj_set_style_pad_row(content, S.rowGapPx, 0);
    lv_obj_set_style_pad_column(content, S.colGapPx, 0);
    // Track distribution: START fills normally (FR columns); CENTER only when we
    // intentionally center a lone tile / single column. (STRETCH is not a valid
    // track-align value and corrupts FR sizing — use START.)
    lv_obj_set_grid_align(content,
                          singleColumn ? LV_GRID_ALIGN_CENTER : LV_GRID_ALIGN_START,
                          singleTile ? LV_GRID_ALIGN_CENTER : LV_GRID_ALIGN_START);

    for (size_t i = 0; i < page.tiles.size(); i++) {
        buildTile(content, page.tiles[i], place[i]);
    }
}

// Free the live page's widgets (and the heap-owned ids) before building another.
void freeCurrentPage() {
    for (auto &kv : cards) delete kv.second.idHolder;
    cards.clear();
    if (currentPage >= 0 && currentPage < (int)pageTiles.size()) {
        lv_obj_clean(pageTiles[currentPage]);
    }
}

// Swipe settled on a new page: free the old one, realize the new one.
void onPageChange(lv_event_t *) {
    if (!tileview) return;
    lv_obj_t *act = lv_tileview_get_tile_act(tileview);
    if (!act) return;
    int idx = -1;
    for (size_t i = 0; i < pageTiles.size(); i++) {
        if (pageTiles[i] == act) { idx = (int)i; break; }
    }
    if (idx < 0 || idx == currentPage) return;
    freeCurrentPage();
    currentPage = idx;
    buildPage(idx);
    updateChrome();
}

void clearDashboard() {
    freeCurrentPage();
    pageTiles.clear();
    currentPage = -1;
    pageDots.clear();           // children freed with the container below
    if (dotsCont) {
        lv_obj_del(dotsCont);
        dotsCont = nullptr;
    }
    if (tileview) {
        lv_obj_del(tileview);  // deletes its tiles too
        tileview = nullptr;
    }
}

// Show the phone-style settings menu (swipe-down target on the dashboard). It
// slides down *over* the dashboard (which stays put), like a notification shade.
void showSettingsMenu() {
    if (menuIpLabel) lv_label_set_text(menuIpLabel, settingsUrl.c_str());
    lv_scr_load_anim(screenMenu, LV_SCR_LOAD_ANIM_OVER_BOTTOM, 250, 0, false);
}

// Swipe down on the dashboard -> open the settings menu.
void onDashGesture(lv_event_t *) {
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_BOTTOM) showSettingsMenu();
}

// Swipe up on the settings menu -> the menu slides out upward, revealing the
// dashboard underneath (only if one exists).
void onMenuGesture(lv_event_t *) {
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP && !pageTiles.empty()) {
        lv_scr_load_anim(screenDash, LV_SCR_LOAD_ANIM_OUT_TOP, 250, 0, false);
    }
}

} // namespace

void uiInit() {
    screenBoot = makeScreen();
    bootLabel = lv_label_create(screenBoot);
    lv_obj_center(bootLabel);
    lv_obj_set_style_text_color(bootLabel, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(bootLabel, fontForPx(40), 0);
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
    lv_obj_set_style_text_font(setupTitle, fontForPx(40), 0);

    setupLabel = lv_label_create(screenSetup);
    lv_obj_set_width(setupLabel, lv_pct(100));
    lv_label_set_long_mode(setupLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(setupLabel, lv_color_hex(0xc8d2db), 0);
    lv_obj_set_style_text_font(setupLabel, fontForPx(24), 0);
    lv_obj_set_style_text_align(setupLabel, LV_TEXT_ALIGN_CENTER, 0);

    setupApLabel = lv_label_create(screenSetup);
    lv_obj_set_width(setupApLabel, lv_pct(100));
    lv_label_set_long_mode(setupApLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(setupApLabel, lv_color_hex(0x1e88e5), 0);
    lv_obj_set_style_text_font(setupApLabel, fontForPx(32), 0);
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
    lv_obj_set_style_text_font(infoTitle, fontForPx(40), 0);

    infoBodyLabel = lv_label_create(screenInfo);
    lv_obj_set_width(infoBodyLabel, lv_pct(100));
    lv_label_set_long_mode(infoBodyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(infoBodyLabel, lv_color_hex(0xc8d2db), 0);
    lv_obj_set_style_text_font(infoBodyLabel, fontForPx(24), 0);
    lv_obj_set_style_text_align(infoBodyLabel, LV_TEXT_ALIGN_CENTER, 0);

    infoUrlLabel = lv_label_create(screenInfo);
    lv_obj_set_width(infoUrlLabel, lv_pct(100));
    lv_label_set_long_mode(infoUrlLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(infoUrlLabel, lv_color_hex(0x1e88e5), 0);
    lv_obj_set_style_text_font(infoUrlLabel, fontForPx(32), 0);
    lv_obj_set_style_text_align(infoUrlLabel, LV_TEXT_ALIGN_CENTER, 0);

    screenDash = makeScreen();

    // Top chrome: a bar (height = icon height) holding the page title centered on
    // the same line as the HA status icon. Both are vertically centered in the bar
    // (no manual offsets). The icon is inset from the corner (rounded panels) and
    // the title's width is reserved so a long title ellipsizes before reaching it.
    lv_obj_t *topBar = lv_obj_create(screenDash);
    lv_obj_remove_style_all(topBar);
    lv_obj_set_size(topBar, lv_pct(100), S.topBarHeightPx);
    lv_obj_align(topBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    connIcon = lv_label_create(topBar);
    lv_obj_set_style_text_font(connIcon, &mdi_font, 0);
    lv_obj_set_style_text_color(connIcon, lv_color_hex(kIconOff), 0);
    lv_label_set_text(connIcon, MDI_HOME_ASSISTANT);
    lv_obj_align(connIcon, LV_ALIGN_RIGHT_MID, -14, 0);

    titleLabel = lv_label_create(topBar);
    const int titleReserve = 64;  // px kept clear on each side (protects the icon)
    lv_obj_set_width(titleLabel, S.screenW - 2 * titleReserve);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(S.nameColor), 0);
    lv_obj_set_style_text_font(titleLabel, fontForPx(S.titleFontPx), 0);
    lv_label_set_text(titleLabel, "");
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, 0);
    // The dashboard tileview + dots are (re)created in uiShowDashboard.

    // Settings / info screen: config-portal URL now, room for controls later.
    // Shown automatically when no dashboard exists, and via a swipe-down from the
    // dashboard (swipe up returns). Disabling scroll-elastic on the page content
    // lets a top-of-page downward pull register as a gesture rather than a scroll.
    screenSettings = makeScreen();
    lv_obj_set_style_pad_all(screenSettings, 22, 0);
    lv_obj_set_flex_flow(screenSettings, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screenSettings, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(screenSettings, 10, 0);
    lv_obj_clear_flag(screenSettings, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *settingsTitle = lv_label_create(screenSettings);
    lv_label_set_text(settingsTitle, "Settings");
    lv_obj_set_style_text_color(settingsTitle, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(settingsTitle, fontForPx(40), 0);

    lv_obj_t *settingsPortalCaption = lv_label_create(screenSettings);
    lv_label_set_text(settingsPortalCaption, "Configuration portal");
    lv_obj_set_style_text_color(settingsPortalCaption, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(settingsPortalCaption, fontForPx(18), 0);

    settingsUrlLabel = lv_label_create(screenSettings);
    lv_obj_set_width(settingsUrlLabel, lv_pct(100));
    lv_label_set_long_mode(settingsUrlLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(settingsUrlLabel, lv_color_hex(kPrimary), 0);
    lv_obj_set_style_text_font(settingsUrlLabel, fontForPx(32), 0);
    lv_obj_set_style_text_align(settingsUrlLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(settingsUrlLabel, "");

    settingsHint = lv_label_create(screenSettings);
    lv_obj_set_width(settingsHint, lv_pct(100));
    lv_label_set_long_mode(settingsHint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(settingsHint, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(settingsHint, fontForPx(18), 0);
    lv_obj_set_style_text_align(settingsHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(settingsHint, 16, 0);
    lv_label_set_text(settingsHint, "");

    // Phone-style settings menu (swipe-down target): a dark title bar with
    // "Settings" left + HA icon right, a lighter panel for future buttons, and
    // the portal URL small at the bottom.
    screenMenu = makeScreen();

    lv_obj_t *menuBar = lv_obj_create(screenMenu);
    lv_obj_remove_style_all(menuBar);
    lv_obj_set_size(menuBar, lv_pct(100), S.topBarHeightPx);
    lv_obj_align(menuBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(menuBar, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi-to-AP signal indicator, inset from the rounded top-left corner.
    menuWifiIcon = lv_label_create(menuBar);
    lv_obj_set_style_text_font(menuWifiIcon, &mdi_font, 0);
    lv_obj_set_style_text_color(menuWifiIcon, lv_color_hex(kIconOff), 0);
    lv_label_set_text(menuWifiIcon, MDI_SIGNAL_CELLULAR_OUTLINE);
    lv_obj_align(menuWifiIcon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *menuTitle = lv_label_create(menuBar);
    lv_label_set_text(menuTitle, "Settings");
    lv_obj_set_style_text_color(menuTitle, lv_color_hex(S.nameColor), 0);
    lv_obj_set_style_text_font(menuTitle, fontForPx(S.titleFontPx), 0);
    lv_obj_align(menuTitle, LV_ALIGN_CENTER, 0, 0);

    menuConnIcon = lv_label_create(menuBar);
    lv_obj_set_style_text_font(menuConnIcon, &mdi_font, 0);
    lv_obj_set_style_text_color(menuConnIcon, lv_color_hex(kIconOff), 0);
    lv_label_set_text(menuConnIcon, MDI_HOME_ASSISTANT);
    lv_obj_align(menuConnIcon, LV_ALIGN_RIGHT_MID, -14, 0);

    const int menuMargin = 12;
    const int menuFooterH = 40;  // footer band; the IP font nearly fills it
    menuPanel = lv_obj_create(screenMenu);
    lv_obj_set_size(menuPanel, S.screenW - 2 * menuMargin,
                    S.screenH - S.topBarHeightPx - menuFooterH);
    lv_obj_align(menuPanel, LV_ALIGN_TOP_MID, 0, S.topBarHeightPx);
    lv_obj_set_style_bg_color(menuPanel, lv_color_hex(S.pressedBg), 0);
    lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(menuPanel, 16, 0);
    lv_obj_set_style_border_width(menuPanel, 0, 0);
    lv_obj_set_style_pad_all(menuPanel, 12, 0);
    lv_obj_set_flex_flow(menuPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(menuPanel, 10, 0);
    // (Setting buttons will be added here later.)

    menuIpLabel = lv_label_create(screenMenu);
    lv_obj_set_style_text_color(menuIpLabel, lv_color_hex(kTextMuted), 0);
    lv_obj_set_style_text_font(menuIpLabel, fontForPx(24), 0);  // fills the footer band
    lv_obj_align(menuIpLabel, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(menuIpLabel, "");

    // Swipe gestures: down on the dashboard opens the menu, up on the menu returns.
    lv_obj_add_event_cb(screenDash, onDashGesture, LV_EVENT_GESTURE, nullptr);
    lv_obj_add_event_cb(screenMenu, onMenuGesture, LV_EVENT_GESTURE, nullptr);

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

void uiSetPortalUrl(const String &url) {
    settingsUrl = "http://" + url + "/";
    if (settingsUrlLabel) lv_label_set_text(settingsUrlLabel, settingsUrl.c_str());
}

void uiShowSettings() {
    if (settingsUrlLabel) lv_label_set_text(settingsUrlLabel, settingsUrl.c_str());
    if (settingsHint) {
        lv_label_set_text(settingsHint,
            pageTiles.empty()
                ? "Open this address to choose the entities to display."
                : "Swipe up to return to the dashboard.");
    }
    lv_scr_load(screenSettings);
}

void uiShowDashboard(const core::Layout &newLayout, const ha::EntityStore &store) {
    clearDashboard();
    layout = newLayout;
    entityStore = &store;

    // Tileview fills the space between the top bar and the bottom dots bar.
    const int tvH = S.screenH - S.topBarHeightPx - S.dotsBarHeightPx;
    tileview = lv_tileview_create(screenDash);
    lv_obj_set_size(tileview, lv_pct(100), tvH);
    lv_obj_align(tileview, LV_ALIGN_TOP_MID, 0, S.topBarHeightPx);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tileview, 0, 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);  // dots replace the bar
    lv_obj_add_event_cb(tileview, onPageChange, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(tileview, onTileviewScroll, LV_EVENT_SCROLL, nullptr);

    for (size_t i = 0; i < layout.pages.size(); i++) {
        lv_obj_t *t = lv_tileview_add_tile(tileview, (uint8_t)i, 0, LV_DIR_HOR);
        lv_obj_set_flex_flow(t, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(t, S.pagePadPx, 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        pageTiles.push_back(t);
    }

    // Bottom chrome: one navigation dot per page (hidden when there's only one).
    if (layout.pages.size() > 1) {
        dotsCont = lv_obj_create(screenDash);
        lv_obj_remove_style_all(dotsCont);
        lv_obj_set_size(dotsCont, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_align(dotsCont, LV_ALIGN_BOTTOM_MID, 0, -9);  // lifted clearly off the edge
        lv_obj_set_flex_flow(dotsCont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(dotsCont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(dotsCont, 8, 0);
        lv_obj_clear_flag(dotsCont, LV_OBJ_FLAG_SCROLLABLE);
        for (size_t i = 0; i < layout.pages.size(); i++) {
            lv_obj_t *dot = lv_obj_create(dotsCont);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 9, 9);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
            pageDots.push_back(dot);
        }
    }

    if (!pageTiles.empty()) {
        currentPage = 0;
        buildPage(0);
    }
    updateChrome();
    lv_scr_load(screenDash);
}

void uiUpdateEntity(const ha::EntityState &e) {
    auto it = cards.find(e.entityId);
    if (it == cards.end()) return;
    applyVisual(it->second, e);
}

// Recolor the corner status badge from the HA connection text (see statusText()
// in main.cpp): green = connected, amber = connecting/authenticating, red = down.
void uiSetConnectionStatus(const String &text) {
    // Order matters: "disconnected" contains "connected", so test the down/failed
    // states first, then the in-progress ones, and only then the connected state.
    uint32_t c = kStatusDown;
    if (text.indexOf("disconnected") >= 0 || text.indexOf("failed") >= 0) c = kStatusDown;
    else if (text.indexOf("connecting") >= 0 || text.indexOf("authenticating") >= 0) c = kAmber;
    else if (text.indexOf("connected") >= 0) c = kStatusOk;
    if (connIcon) lv_obj_set_style_text_color(connIcon, lv_color_hex(c), 0);
    if (menuConnIcon) lv_obj_set_style_text_color(menuConnIcon, lv_color_hex(c), 0);
}

void uiSetWifiStatus(bool connected, int rssi) {
    if (!menuWifiIcon) return;
    const char *glyph = MDI_SIGNAL_CELLULAR_OUTLINE;  // empty bars when offline
    uint32_t c = kIconOff;                            // grayed out when offline
    if (connected) {
        c = kPrimary;                                 // blue when connected
        if (rssi >= -60) glyph = MDI_SIGNAL_CELLULAR_3;
        else if (rssi >= -72) glyph = MDI_SIGNAL_CELLULAR_2;
        else glyph = MDI_SIGNAL_CELLULAR_1;
    }
    lv_label_set_text(menuWifiIcon, glyph);
    lv_obj_set_style_text_color(menuWifiIcon, lv_color_hex(c), 0);
}

} // namespace ui
