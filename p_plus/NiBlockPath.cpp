#pragma once
#include "p_plus/NiBlockPathBuilder.hpp"
#include "p_plus/NiBlockPath.hpp"

bool NiBlockPathView::contains(const NiBlockPathBuilder& query) const noexcept {

    if (query.empty() || query.length > _length) return false;

    const uint32_t window = query.length;
    for (uint32_t start = 0; start + window <= _length; ++start) {
        bool match = true;
        for (uint8_t i = 0; i < window; ++i) {
            const NiFixedString& mine = _nodes[start + i];
            const NiFixedString& theirs = query.parents[i].getBlockName();
            if (mine != theirs) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}