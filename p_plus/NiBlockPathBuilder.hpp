#pragma once
#include "NiBlockPath.hpp"

constexpr size_t MaxDepth = 64; //Increase this if necessary
struct NiBlockPathBuilder {

    struct Frame {

        const NiNode*   node;
        UInt8           segment, next;

        Frame() = default;
        Frame(const NiNode* _node, UInt8 seg, UInt8 itr) noexcept
            : node(_node)
            , segment(seg)
            , next(itr)
        {}

        const NiFixedString& getBlockName() const { return node->m_blockName; };

        //Move method
        operator NiFixedString() const {
            return NiFixedString(this->getBlockName());
        }

    };

    std::array<Frame, MaxDepth>     parents;           // stack-allocated buffer
    UInt8                           length{ 0 };       // how many entries are valid
    UInt8                           cacheStart{ MaxDepth };    // This grows backwards

    NiBlockPathBuilder() = default;

    inline const Frame& getCurrentFrame() const noexcept { return parents[length - 1]; }
    inline UInt8        nextChild() noexcept {
        parents[length - 1].next += 1;
        return parents[length - 1].next - 1; 
    }

    [[nodiscard]] bool isInCache(const NiNode* _node) const noexcept {
        for (size_t i = cacheStart; i < MaxDepth; ++i) {
            if (parents[i].node == _node)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool isParent(const NiNode* _node) const noexcept {
        for (size_t i = 0; i < length; ++i) {
            if (parents[i].node == _node)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool contains(const NiNode* _node) const noexcept {
        return isInCache(_node) || isParent(_node);
    }

    // Enters child node, and skips if we've already entered this one.
    inline void enterChildNode(const NiNode* _node, UInt16 segment, UInt16 startFromChildIndex = 0) {
        if (!contains(_node)) {
            parents[length++] = Frame(_node, segment, startFromChildIndex);
        }
    }

    //Same as enter child node
    inline void captureLastFrame(const NiNode* _node, UInt16 segment = 0, UInt16 iter = 0) {
        parents[length++] = Frame(_node, segment, iter);
    }

    inline UInt16 getCurrentSegment() const {
        return parents[length - 1].segment;
    }

    inline const NiNode* getCurrentNode() {
        return parents[length - 1].node;
    }

    inline void pop() noexcept {
        // Snapshot current top frame
        const Frame& f = parents[length - 1];

        // If its refcount >1, and there's still room between front and cacheStart
        if (f.node->m_uiRefCount > 1 && cacheStart > length) {
            // carve out one slot at cacheStart-1
            parents[--cacheStart] = f;
        }

        // Remove from the front‐stack
        --length;
    }

    inline void clear() noexcept {
        length = 0;
    }

    [[nodiscard]] UInt8 size()   const noexcept { return length; }
    [[nodiscard]] bool  empty()  const noexcept { return length == 0; }


    NiBlockPathStatic toStaticPath(const NiBlockPathView& originalPath) {
        // Move the segments into the new heap buffer
        if (empty()) {
            return NiBlockPathStatic();
        }
        return NiBlockPathStatic{ parents.begin(), parents.begin() + length };
    }

};