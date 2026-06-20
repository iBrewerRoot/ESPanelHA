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

    /** Only store entities in these domains (e.g. {"light","switch"}). HA's full
     *  state dump is ~200 KB across all domains; keeping only what the UI uses
     *  bounds memory. Empty filter (default) accepts everything. */
    void setDomainFilter(std::vector<String> domains) { domainFilter_ = std::move(domains); }

    /** Upsert an entity. Notifies the listener if registered. Returns false (and
     *  stores nothing) for entities outside the domain filter. */
    bool update(const EntityState &e);

    const EntityState *get(const String &entityId) const;

    /** Entities whose domain is one of `domains` (e.g. controllable ones). */
    std::vector<core::AvailableEntity> listByDomain(
        const std::vector<String> &domains) const;

    void onChange(ChangeCb cb) { changeCb_ = std::move(cb); }

    /** Helper: domain part of "light.kitchen" -> "light". */
    static String domainOf(const String &entityId);

private:
    bool domainAllowed(const String &domain) const;

    std::map<String, EntityState> entities_;
    std::vector<String> domainFilter_;
    ChangeCb changeCb_;
};

} // namespace ha

#endif /* HA_ENTITY_STORE_H */
