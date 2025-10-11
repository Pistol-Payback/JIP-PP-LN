#pragma once
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

// Generic registry: maps T* -> vector<Owner*>
// Owner must provide: using index_t; static constexpr index_t kInvalid;
template<class T, class Owner>
struct pWeakRegistry {
    using index_t = typename Owner::index_t;
    static constexpr index_t kInvalid = Owner::kInvalid;

    using Bucket = std::vector<Owner*>;
    std::unordered_map<T*, Bucket> buckets;

    // Register owner into obj's bucket; returns index
    index_t add(T* obj, Owner* owner) {
        if (!obj || !owner) return kInvalid;
        auto& list = buckets[obj];
        list.push_back(owner);
        return static_cast<index_t>(list.size() - 1);
    }

    // O(1) remove via swap-pop; updates moved owner's index
    void remove(T* obj, index_t idx) {
        if (!obj) return;
        auto it = buckets.find(obj);
        if (it == buckets.end()) return;

        auto& list = it->second;
        if (idx == kInvalid || idx >= list.size()) return;

        const index_t last = static_cast<index_t>(list.size() - 1);
        if (idx != last) {
            Owner* moved = list[last];
            list[idx] = moved;
            moved->registryIndex = idx; // Owner must expose this field
        }
        list.pop_back();
        if (list.empty()) buckets.erase(it);
    }

    // Owner moved in memory; keep same obj/idx but update pointer
    void assignNewOwner(T* obj, index_t idx, Owner* newOwner) {
        if (!obj || !newOwner) return;
        auto it = buckets.find(obj);
        if (it == buckets.end()) return;
        auto& list = it->second;
        if (idx < list.size()) list[idx] = newOwner;
    }

    // Invalidate all owners for obj and remove bucket
    void destroy_object(T* obj) {
        if (!obj) return;
        auto it = buckets.find(obj);
        if (it == buckets.end()) return;

        Bucket owners = std::move(it->second);
        buckets.erase(it);
        for (Owner* w : owners) {
            if (!w) continue;
            w->invalidate(); // Owner must implement invalidate()
        }
    }

    // Change one owner's target: (old -> new)
    index_t change_target(Owner* owner, T* newObj, T* oldObj, index_t oldIdx) {
        if (oldObj && oldIdx != kInvalid) remove(oldObj, oldIdx);
        if (!newObj) return kInvalid;
        return add(newObj, owner);
    }

    // Move *all* owners from oldObj to newObj, keeping them alive
    void retarget_all(T* oldObj, T* newObj) {
        if (!oldObj || oldObj == newObj) return;
        auto it = buckets.find(oldObj);
        if (it == buckets.end()) return;

        Bucket owners = std::move(it->second);
        buckets.erase(it);

        auto& dst = buckets[newObj];
        dst.reserve(dst.size() + owners.size());
        for (Owner* w : owners) {
            if (!w) continue;
            w->ptr_ = newObj; // Owner must expose ptr_ for fast swap
            w->registryIndex = static_cast<index_t>(dst.size());
            dst.push_back(w);
        }
    }
};

// Global accessor for any WeakRegistry<T,Owner>
template<class T, class Owner>
inline pWeakRegistry<T, Owner>& GetWeakRegistry() {
    static pWeakRegistry<T, Owner> gRegistry;
    return gRegistry;
}