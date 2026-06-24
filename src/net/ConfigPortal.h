/**
 * Config web app (runs in station mode, once WiFi is up).
 *
 * Serves a small single-page app (gzipped, embedded in flash) that talks to a
 * JSON REST API on the device: Home Assistant settings, a multi-page dashboard
 * layout editor, sensor/entity discovery, optional HTTP Basic auth, and OTA.
 * The portal is decoupled from the rest of the firmware through the callbacks
 * below; all persistence/UI work is deferred to the main loop by the callees.
 */
#ifndef NET_CONFIG_PORTAL_H
#define NET_CONFIG_PORTAL_H

#include "core/AppConfig.h"

#include <functional>
#include <vector>

namespace net {

struct ConfigPortalCallbacks {
    // Current HA settings (to prefill; token is never sent to the browser).
    std::function<core::HAConfig()> currentHa;
    // New HA settings on save (persist + (re)connect).
    std::function<void(core::HAConfig)> onSaveHa;
    // Whole selectable-entity catalog as a JSON array string [{id,name,domain,icon}].
    std::function<String()> entitiesCatalogJson;
    // Device dashboard spec (screen geometry + style tokens) as a JSON object, so
    // the web editor draws a board-accurate, pixel-faithful WYSIWYG preview.
    std::function<String()> deviceSpecJson;
    // Current dashboard layout (to populate the editor).
    std::function<core::Layout()> currentLayout;
    // New layout on save (persist + rebuild the on-screen dashboard).
    std::function<void(core::Layout)> onSaveLayout;
    // New auth settings; empty password means "keep the stored hash".
    std::function<void(bool enabled, String user, String password)> onSaveAuth;
    // Current display settings (orientation + columns), to prefill the editor.
    std::function<core::DisplayConfig()> currentDisplay;
    // New display settings on save (persist + re-apply orientation/grid live).
    std::function<void(core::DisplayConfig)> onSaveDisplay;
    // Current power settings (brightness + sleep + battery), to prefill the editor.
    std::function<core::PowerConfig()> currentPower;
    // New power settings on save (persist + apply brightness/sleep thresholds).
    std::function<void(core::PowerConfig)> onSavePower;
    // Factory reset (wipe NVS + layout).
    std::function<void()> onFactoryReset;
};

/** Cached auth state used by the (synchronous) request guard, so handlers never
 *  touch NVS. Refreshed at boot and after every auth change. */
struct PortalAuth {
    bool enabled = false;
    String user;
    String salt;  // hex
    String hash;  // hex, SHA-256(salt || password)
};

void configPortalBegin(const ConfigPortalCallbacks &cb, const PortalAuth &auth);

/** Update the cached auth used by the request guard (call after a save). */
void configPortalSetAuth(const PortalAuth &auth);

/** Call frequently from the main loop. Handles the deferred reboot that follows
 *  a firmware upload or a factory reset (so the HTTP response flushes first). */
void configPortalLoop();

} // namespace net

#endif /* NET_CONFIG_PORTAL_H */
