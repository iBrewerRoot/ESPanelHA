/**
 * Persistent storage.
 *   - HA host/port/TLS + (encrypted) token and config-portal auth in NVS.
 *   - Dashboard layout (pages + tiles) in /layout.json on LittleFS.
 *
 * A legacy /entities.json (flat selection) is migrated to a single-page layout
 * on first load.
 */
#ifndef NET_STORAGE_H
#define NET_STORAGE_H

#include "core/AppConfig.h"

namespace net {

/** Mount LittleFS. Returns false if the filesystem could not be mounted. */
bool storageBegin();

/** Populate ha (token decrypted), layout (migrating legacy data) and auth. */
void loadConfig(core::AppConfig &out);

void saveHAConfig(const core::HAConfig &ha);   // token stored encrypted
void saveLayout(const core::Layout &layout);   // clamped to core::kMax* caps
void saveAuth(const core::AuthConfig &auth);
void saveDisplayConfig(const core::DisplayConfig &display);  // orientation + columns
void savePowerConfig(const core::PowerConfig &power);        // brightness + sleep + battery

/** Wipe all stored settings (factory reset). */
void clearAll();

} // namespace net

#endif /* NET_STORAGE_H */
