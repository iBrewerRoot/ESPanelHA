/**
 * Dashboard geometry + style tokens — the SINGLE source of truth shared by the
 * on-device renderer (UiManager) and the web editor's pixel-faithful WYSIWYG
 * preview (served as JSON by the config portal via a callback).
 *
 * Board-agnostic by design: screen size / board name are filled from the board
 * HAL where dashboardSpec() is defined (in UiManager.cpp, which may know the
 * board), so net/ stays board-free. UiManager reads these same values when it
 * builds the on-screen tiles, so the browser preview can never drift from the
 * device.
 */
#ifndef UI_UI_THEME_H
#define UI_UI_THEME_H

#include <stdint.h>

namespace ui {

struct DashboardSpec {
    const char *boardName;
    int screenW, screenH;     // panel size in px

    int gridCols;             // columns of the tile grid (packing assumes this)
    int rowHeightPx;          // px per grid row (h=2 spans two rows)
    int colGapPx, rowGapPx;   // gaps between tiles
    int pagePadPx;            // padding around a page's content
    int pageTitleGapPx;       // gap below the page title
    int topBarHeightPx;       // top chrome: centered title + status icon
    int dotsBarHeightPx;      // bottom chrome: page-indicator dots
    // tileview fills the space between the two bars (screenH - top - dots)

    int tileRadiusPx, tilePadPx, tileGapPx;  // tile box
    int iconSizePx, nameFontPx, stateFontPx, titleFontPx, sliderHeightPx;

    // Value/state text shown right of the icon — size grows with the tile so a
    // bigger tile reads from across the room. Picked by valueFontFor(w, h).
    int valueFontSmallPx;   // 1x1
    int valueFontMedPx;     // 2x1 or 1x2
    int valueFontLargePx;   // 2x2

    // Colors packed 0xRRGGBB (mirror the LVGL palette).
    uint32_t screenBg, tileBg, pressedBg, iconOff, amber, primary, textMuted, nameColor;
    int activeMix;            // accent-over-tileBg blend (0..255), lv_color_mix ratio
};

/** The active board's dashboard spec (geometry + style). Defined in UiManager,
 *  where the board profile (screen size / name) is in scope. */
const DashboardSpec &dashboardSpec();

/** Value-text size for a tile of (w, h) cells. Wide tiles get the largest font;
 *  shared by the device renderer and the web preview so both stay in sync. */
inline int valueFontFor(const DashboardSpec &s, int w, int h) {
    if (w >= 2 && h >= 2) return s.valueFontLargePx;
    if (w >= 2 || h >= 2) return s.valueFontMedPx;
    return s.valueFontSmallPx;
}

} // namespace ui

#endif /* UI_UI_THEME_H */
