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
#include <set>
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

    /** Domains offered to the user (e.g. {"light","switch","sensor"}). Entities
     *  outside these are ignored entirely. Empty filter accepts everything. */
    void setDomainFilter(std::vector<String> domains) { domainFilter_ = std::move(domains); }

    /** Entities to keep FULL state for (those shown on the dashboard). All other
     *  selectable entities only contribute to the lightweight catalog, so the
     *  resident footprint stays bounded by the layout, not the HA instance size. */
    void setInterest(const std::vector<String> &entityIds) {
        interest_ = std::set<String>(entityIds.begin(), entityIds.end());
    }

    /** Snapshot ingestion (one call per entity while streaming /api/states):
     *  appends to the catalog (if its domain is allowed) and stores full state
     *  only for entities of interest. Bracket calls with begin/endCatalog(). */
    void beginCatalog();
    bool ingest(const EntityState &e);
    void endCatalog();

    /** Live update (state_changed). Stores/notifies only for entities of
     *  interest; others are dropped. Returns true if applied. */
    bool update(const EntityState &e);

    const EntityState *get(const String &entityId) const;

    /** Lightweight JSON catalog [{id,name,domain},...] of all selectable
     *  entities — served verbatim to the web editor (one contiguous block). */
    const String &catalogJson() const { return catalogJson_; }

    /** True if no snapshot catalog has been built (used to retry a failed fetch). */
    bool snapshotEmpty() const { return catalogJson_.length() <= 2; }

    void onChange(ChangeCb cb) { changeCb_ = std::move(cb); }

    /** Helper: domain part of "light.kitchen" -> "light". */
    static String domainOf(const String &entityId);

private:
    bool domainAllowed(const String &domain) const;

    std::map<String, EntityState> entities_;  // full state, interest-only
    std::set<String> interest_;               // entity ids on the dashboard
    std::vector<String> domainFilter_;
    String catalogJson_ = "[]";               // lightweight picker catalog
    bool catalogOpen_ = false;
    bool catalogFirst_ = true;
    ChangeCb changeCb_;
};

} // namespace ha

#endif /* HA_ENTITY_STORE_H */
