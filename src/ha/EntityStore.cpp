#include "EntityStore.h"

#include <ArduinoJson.h>

namespace ha {

String EntityStore::domainOf(const String &entityId) {
    const int dot = entityId.indexOf('.');
    return dot > 0 ? entityId.substring(0, dot) : String();
}

bool EntityStore::domainAllowed(const String &domain) const {
    if (domainFilter_.empty()) return true;
    for (const auto &d : domainFilter_) {
        if (d == domain) return true;
    }
    return false;
}

void EntityStore::beginCatalog() {
    catalogJson_ = "[";
    catalogJson_.reserve(20000);  // one contiguous block -> low heap fragmentation
    catalogOpen_ = true;
    catalogFirst_ = true;
    entities_.clear();  // rebuilt for the current interest set during the snapshot
}

bool EntityStore::ingest(const EntityState &e) {
    String domain = e.domain.length() ? e.domain : domainOf(e.entityId);
    if (!domainAllowed(domain)) return false;

    // Lightweight catalog entry for the web editor (escaped via ArduinoJson).
    if (catalogOpen_) {
        if (!catalogFirst_) catalogJson_ += ',';
        catalogFirst_ = false;
        JsonDocument d;
        d["id"] = e.entityId;
        d["name"] = e.friendlyName.length() ? e.friendlyName : e.entityId;
        d["domain"] = domain;
        String one;
        serializeJson(d, one);
        catalogJson_ += one;
    }

    // Full state only for entities actually shown on the dashboard.
    if (interest_.count(e.entityId)) {
        EntityState merged = e;
        merged.domain = domain;
        entities_[e.entityId] = merged;
    }
    return true;
}

void EntityStore::endCatalog() {
    if (!catalogOpen_) return;
    catalogJson_ += "]";
    catalogOpen_ = false;
}

bool EntityStore::update(const EntityState &e) {
    String domain = e.domain.length() ? e.domain : domainOf(e.entityId);
    if (!domainAllowed(domain)) return false;
    if (!interest_.count(e.entityId)) return false;  // not displayed -> ignore
    EntityState merged = e;
    merged.domain = domain;
    entities_[merged.entityId] = merged;
    if (changeCb_) changeCb_(merged);
    return true;
}

const EntityState *EntityStore::get(const String &entityId) const {
    auto it = entities_.find(entityId);
    return it == entities_.end() ? nullptr : &it->second;
}

} // namespace ha
