/**
 * LVGL UI layer.
 *
 * Resolution-agnostic: the dashboard is a responsive flex grid of entity cards
 * sized with percentages, so it reflows on every supported panel size.
 * Three screens: boot, setup (shown until configured), dashboard.
 */
#ifndef UI_UI_MANAGER_H
#define UI_UI_MANAGER_H

#include "core/AppConfig.h"
#include "ha/EntityStore.h"

#include <Arduino.h>
#include <functional>
#include <vector>

namespace ui {

struct Actions {
    std::function<void(const String &entityId)> onToggle;
    std::function<void(const String &entityId, int pct)> onBrightness;
};

void uiInit();
void uiSetActions(const Actions &actions);

void uiShowBoot(const String &message);
void uiShowSetup(const String &apName);

/** Shown when WiFi is up but Home Assistant isn't configured yet: tells the
 *  user which address to open to enter the HA host/token. */
void uiShowConfigureHa(const String &ip);

/** Shown once WiFi/HA are connected but no entities are chosen yet: tells the
 *  user which address to open to pick entities. */
void uiShowConnected(const String &ip);

/** Build the swipeable, multi-page dashboard from the layout. Only the visible
 *  page's widgets are realized at a time (memory-conscious for the C6). */
void uiShowDashboard(const core::Layout &layout, const ha::EntityStore &store);

/** Refresh a single card from a new state (no-op if not on the dashboard). */
void uiUpdateEntity(const ha::EntityState &e);

/** Top-bar connection indicator text. */
void uiSetConnectionStatus(const String &text);

} // namespace ui

#endif /* UI_UI_MANAGER_H */
