/**
 * Entity-selection web portal (runs in station mode, after WiFi+HA are up).
 *
 * Serves a page listing the entities discovered from Home Assistant with a
 * checkbox each; the user picks which ones appear on the dashboard. The portal
 * is decoupled from the HA layer through the providers below.
 */
#ifndef NET_CONFIG_PORTAL_H
#define NET_CONFIG_PORTAL_H

#include "core/AppConfig.h"

#include <functional>
#include <vector>

namespace net {

struct ConfigPortalCallbacks {
    // Current Home Assistant settings (to prefill the form).
    std::function<core::HAConfig()> currentHa;
    // Called with new HA settings on save (persist + (re)connect).
    std::function<void(core::HAConfig)> onSaveHa;
    // All controllable entities discovered from HA (id, friendly name, domain).
    std::function<std::vector<core::AvailableEntity>()> listAvailable;
    // Currently selected entities (to pre-check the boxes).
    std::function<std::vector<core::SelectedEntity>()> currentSelection;
    // Called with the new selection on save (persist + apply to the dashboard).
    std::function<void(std::vector<core::SelectedEntity>)> onSave;
};

void configPortalBegin(const ConfigPortalCallbacks &cb);

} // namespace net

#endif /* NET_CONFIG_PORTAL_H */
