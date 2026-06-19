/**
 * Persistent storage.
 *   - HA host/port/token in NVS (Preferences).
 *   - Selected entities (id + label + order) in /entities.json on LittleFS.
 */
#ifndef NET_STORAGE_H
#define NET_STORAGE_H

#include "core/AppConfig.h"

namespace net {

/** Mount LittleFS. Returns false if the filesystem could not be mounted. */
bool storageBegin();

void loadConfig(core::AppConfig &out);

void saveHAConfig(const core::HAConfig &ha);
void saveEntities(const std::vector<core::SelectedEntity> &entities);

/** Wipe all stored settings (factory reset). */
void clearAll();

} // namespace net

#endif /* NET_STORAGE_H */
