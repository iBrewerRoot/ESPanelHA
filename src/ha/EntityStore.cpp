#include "EntityStore.h"

namespace ha {

String EntityStore::domainOf(const String &entityId) {
    const int dot = entityId.indexOf('.');
    return dot > 0 ? entityId.substring(0, dot) : String();
}

void EntityStore::update(const EntityState &e) {
    EntityState merged = e;
    if (merged.domain.length() == 0) {
        merged.domain = domainOf(e.entityId);
    }
    entities_[merged.entityId] = merged;
    if (changeCb_) changeCb_(merged);
}

const EntityState *EntityStore::get(const String &entityId) const {
    auto it = entities_.find(entityId);
    return it == entities_.end() ? nullptr : &it->second;
}

std::vector<core::AvailableEntity> EntityStore::listByDomain(
    const std::vector<String> &domains) const {
    std::vector<core::AvailableEntity> out;
    for (const auto &kv : entities_) {
        const EntityState &e = kv.second;
        bool match = false;
        for (const auto &d : domains) {
            if (e.domain == d) { match = true; break; }
        }
        if (!match) continue;
        core::AvailableEntity a;
        a.entityId = e.entityId;
        a.friendlyName = e.friendlyName;
        a.domain = e.domain;
        out.push_back(a);
    }
    return out;
}

} // namespace ha
