#pragma once
#include "VectorSortedView.hpp"
#include "NiRuntimeNode.hpp"
#include <set>

struct NiRuntimeNodeVector {

    struct NiRuntimeNodeDepthCompare {
        bool operator()(const NiRuntimeNode& a, const NiRuntimeNode& b) const noexcept
        {
            if (a.path.size() != b.path.size())
                return a.path.size() < b.path.size();

            return a.path.raw < b.path.raw;

        }
    };

    VectorSortedView<NiRuntimeNode, NiRuntimeNodeDepthCompare> allPaths;
    //std::set<NiRuntimeNode, NiRuntimeNodeDepthCompare> allPaths;

    NiRuntimeNodeVector() = default;
    NiRuntimeNodeVector(NiRuntimeNodeVector const&) = default;
    NiRuntimeNodeVector& operator=(NiRuntimeNodeVector const&) = default;
    NiRuntimeNodeVector& operator=(NiRuntimeNodeVector&&) noexcept = default;

    template<typename N>
    explicit NiRuntimeNodeVector(N&& nodePath) : allPaths()
    {
        allPaths.reserve(1);
        allPaths.emplace_back(std::forward<N>(nodePath));
    }

    /// Find a matching NiNodePath, or nullptr if none found
    const NiRuntimeNode* findNode(const NiBlockPathBase& toFind) const noexcept {
        // Nothing to look for?
        if (toFind.size() == 0)
            return nullptr;

        // Pick off the leaf segment
        const NiFixedString& leaf = toFind.back();

        // Make a prefix copy with length-1
        NiBlockPathView prefix = toFind.makeView(toFind.size() - 1);

        // Scan the storage
        const auto& storage = allPaths.getStorage();
        for (auto const& existing : storage) {
            if (existing.hasNode(prefix, leaf)) {
                return &existing;
            }
        }
        return nullptr;
    }

    const NiRuntimeNode* findNode(const char* pathToMatch, const char* toFind) const noexcept {
        const auto& storage = allPaths.getStorage();
        for (auto const& existing : storage) {
            if (existing.hasNode(pathToMatch, toFind))
                return &existing;
        }
        return nullptr;
    }

    const NiRuntimeNode* lookupExact(const NiRuntimeNode& toFind) const noexcept {
        for (auto const& existing : allPaths.getStorage()) {
            if (existing == toFind)
                return &existing;
        }
        return nullptr;
    }

    bool containsPath(const NiBlockPath& toFind) {
        const auto& storage = allPaths.getStorage();
        for (auto const& node : storage) {
            if (node.path.contains(toFind))
                return true;
        }
    }

    /// True if there is an exact match already in the list.
    bool containsExact(const NiRuntimeNode& toFind) const noexcept {
        return lookupExact(toFind) != nullptr;
    }

    /// Add an exact path (no suffix‐matching) if missing.
    bool addExact(NiRuntimeNode&& toAdd) {

        if (!toAdd.isValid()) return false;
        if (containsExact(toAdd)) return false;

        allPaths.emplace(std::move(toAdd));
        return true;
    }

    /// Remove a nodes entry, as well as its children.
    size_t remove(const char* pathToMatch, const char* value) {
        return allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return node.contains(pathToMatch, value);
            }
        );
    }

    bool empty() const noexcept {
        return allPaths.empty();
    }

};