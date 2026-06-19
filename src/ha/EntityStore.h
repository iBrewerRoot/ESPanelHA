/**
 * In-memory model of Home Assistant entity states — the single source of truth
 * for the UI. Fed by HAClient (initial get_states + incremental state_changed).
 */
#ifndef HA_ENTITY_STORE_H
#define HA_ENTITY_STORE_H

#include "core/AppConfig.h"

#include <Arduino.h>
#include <functional>
#include <map>
#include <vector>

namespace ha {

struct EntityState {
    String entityId;
    String domain;        // derived from entityId prefix
    String friendlyName;
    String state;         // "on" / "off" / value
    int brightness = -1;  // light only, 0..255, -1 if unknown/unsupported

    // Presentation attributes mirrored from HA, so the UI looks "linked" to it.
    String icon;          // mdi name from attributes.icon, e.g. "mdi:lightbulb" ("" if none)
    String unit;          // sensor unit_of_measurement, e.g. "°C" ("" if none)
    String deviceClass;   // attributes.device_class, e.g. "temperature" ("" if none)
    int32_t rgb = -1;     // light color packed 0xRRGGBB, -1 if unknown/unsupported
};

class EntityStore {
public:
    using ChangeCb = std::function<void(const EntityState &)>;

    /** Upsert an entity. Notifies the listener if registered. */
    void update(const EntityState &e);

    const EntityState *get(const String &entityId) const;

    /** Entities whose domain is one of `domains` (e.g. controllable ones). */
    std::vector<core::AvailableEntity> listByDomain(
        const std::vector<String> &domains) const;

    void onChange(ChangeCb cb) { changeCb_ = std::move(cb); }

    /** Helper: domain part of "light.kitchen" -> "light". */
    static String domainOf(const String &entityId);

private:
    std::map<String, EntityState> entities_;
    ChangeCb changeCb_;
};

} // namespace ha

#endif /* HA_ENTITY_STORE_H */
