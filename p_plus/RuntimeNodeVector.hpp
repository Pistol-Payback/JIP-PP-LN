#pragma once
#include "VectorSortedView.hpp"
#include "NiRuntimeNode.hpp"
//#include <set>

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
    NiRuntimeNodeVector(NiRuntimeNodeVector&&) = default;

    NiRuntimeNodeVector& operator=(NiRuntimeNodeVector const&) = default;
    NiRuntimeNodeVector& operator=(NiRuntimeNodeVector&&) noexcept = default;

    explicit NiRuntimeNodeVector(NiRuntimeNode&& nodePath) {
        allPaths.reserve(1);
        allPaths.emplace_back(std::move(nodePath));
    }

    std::vector<NiRuntimeNode*> getAllContainingPaths(const NiBlockPathBase& toFind) const {

        std::vector<NiRuntimeNode*> out;
        out.reserve(allPaths.size());
        if (toFind.size() == 0)
            return out;

        // Pick off the leaf segment
        const NiFixedString& leaf = toFind.back();
        NiBlockPathView prefix = toFind.makeView(toFind.size() - 1);

        // Scan the storage
        for (auto existing : allPaths) {
            if (existing->path.containsSparsePath(toFind) || existing->isNode(prefix, leaf)) {
                out.push_back(existing);
            }
        }
        return out;
    }

    std::vector<NiRuntimeNode> moveAllContainingPaths(const NiBlockPathBase& toFind) {

        std::vector<NiRuntimeNode> out;

        if (toFind.size() == 0)
            return out;

        // pick off the leaf and its prefix
        const NiFixedString& leaf = toFind.back();
        NiBlockPathView    prefix = toFind.makeView(toFind.size() - 1);
        out.reserve(allPaths.size());

        // walk allPaths, move‑out matching nodes, erase them
        for (NiRuntimeNode& node : allPaths.getStorage()) {
            if (node.path.containsSparsePath(toFind) || node.isNode(prefix, leaf)) {
                out.emplace_back(std::move(node));
            }
        }

        // Erase those same ones from allPaths
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.node.isValid();
            }
        );

        return out;
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
            if (existing.isNode(prefix, leaf)) {
                return &existing;
            }
        }
        return nullptr;
    }

    const NiRuntimeNode* findNode(const char* pathToMatch, const char* toFind) const noexcept {
        const auto& storage = allPaths.getStorage();
        for (auto const& existing : storage) {
            if (existing.isNode(pathToMatch, toFind))
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

    bool containsPath(const NiBlockPathStatic& toFind) {
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

    bool addModel(NiNode* parent, NiBlockPathBase& attachPath, const char* modelPath, const char* suffix) {

        if (!modelPath || !modelPathExists(modelPath)) {
            return false;
        }

        NiBlockPathBuilder builderPath;
        NiNode* node = (NiNode*)parent->BuildNiPath(attachPath, builderPath);
        if (!node || !node->isNiNode()) {
            return false;
        }

        ModelTemp tempModel = ModelTemp(modelPath);

        if (!tempModel.clonedNode) {
            return false; //Model failed to load
        }

        if (!node->hasChildNode(tempModel.clonedNode->m_blockName)) {

            NiNode* toAttach = tempModel.detachNode();

            toAttach->RemoveCollision(); //Prevents invalid collision
            toAttach->AddSuffixToAllChildren(suffix);
            toAttach->DownwardsInitPointLights(parent);
            toAttach->AddPointLights();
            node->AddObject(toAttach, true);

            return addExact(
                NiRuntimeNode(builderPath.toStaticPath(NiBlockPathBase{ attachPath, parent->m_blockName }), toAttach->m_blockName, NiToken(modelPath, suffix))
            );

            //node->UpdateTransformAndBounds(kNiUpdateData);

        }

        return false;

    }

    //Dont call on root
    bool insertParentNode(NiBlockPathBase& original, NiBlockPathBuilder& builderPath, NiNode* node, const char* nodeName) {

        NiFixedString name(nodeName + 1); //Skip the ^ parent syntax

        if (!node->m_parent || node->m_parent->m_blockName == name) return false;

        // Splice all existing NiRuntimeNode paths
        updatePathsOnNodeInsert(builderPath, name);
        NiNode* newParent = NiNode::pCreate(name);

        // snapshot the parent‑path prefix
        builderPath.pop();
        NiBlockPathStatic parentPath = builderPath.toStaticPath(original);

        // Splice into the scene graph
        NiNode* grandparent = node->m_parent;

        // re‑parent the original node under our newParent
        newParent->AddObject(node, true);
        grandparent->AddObject(newParent, true);
        newParent->m_flags |= NiAVObject::NiFlags::kNiFlag_IsInserted;

        grandparent->UpdateTransformAndBounds(kNiUpdateData);

        // Register the new parent in allPaths
        return addExact(NiRuntimeNode(std::move(parentPath), newParent->m_blockName, NiToken(IndexLinkedList(node->m_blockName))));
    }

    /// Inserts a new child under `node` in the graph
    /// and registers it in the runtime‑vector.
    bool insertChildNode(NiBlockPathBase& original, NiBlockPathBuilder& builderPath, NiNode* parent, const char* nodeName) {

        NiFixedString name(nodeName);
        if (!parent || !parent->isNiNode() || parent->hasChildNode(name))
            return false;

        NiNode* child = NiNode::pCreate(name);
        if (!child) return false;

        parent->AddObject(child, true);
        parent->UpdateTransformAndBounds(kNiUpdateData);
        child->m_flags |= NiAVObject::NiFlags::kNiFlag_IsInserted;

        return addExact(NiRuntimeNode(builderPath.toStaticPath(NiBlockPathBase{ original, parent->m_blockName }), child->m_blockName));
    }

    bool attachNode(NiNode* parent, NiBlockPathBase& attachPath, const char* nodeName) {

        if (!parent || !nodeName) return false;

        NiBlockPathBuilder builderPath;
        NiNode* node = static_cast<NiNode*>(parent->BuildNiPath(attachPath, builderPath));

        if (!node) return false;

        if (nodeName[0] == '^') {
            if (node == parent) return false;
            return insertParentNode(attachPath, builderPath, node, nodeName);

        }
        else {
            return insertChildNode(attachPath, builderPath, node, nodeName);
        }

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

    void updatePathsOnNodeInsert(const NiBlockPathBuilder& parentPath, const NiFixedString& segName) {
        const size_t insertAt = parentPath.size() - 1;
        for (auto& node : allPaths.getStorage()) {
            if (node.path.contains(parentPath)) {
                node.path.insertSegment(insertAt, segName);
            }
        }
        allPaths.resort();
    }

    void updatePathsOnNodeRename(const NiBlockPathStatic& parentPath, const NiFixedString& oldName, const NiFixedString& newName) {

        const size_t depth = parentPath.size() - 1;

        for (auto& node : allPaths.getStorage()) {
            if (node.path.contains(parentPath) &&
                node.path[depth] == oldName)
            {
                node.path.replaceSegment(depth, newName);
            }
            // also update the leaf name if it matches
            if (node.node == oldName) {
                node.node = newName;
            }
        }
        allPaths.resort();

    }

    void updatePathsOnNodeRemove(const NiBlockPathStatic& parentPath, const NiFixedString& nodeToRemove) {

        size_t removeAt = parentPath.size() - 1;
        for (auto& node : allPaths.getStorage()) {
            if (node.path.contains(parentPath)) {
                node.path.removeSegment(removeAt);
            }
            // also fix the leaf name if it was the removed node:
            if (node.node == nodeToRemove) {
                node.node = NiFixedString();
            }
        }
        allPaths.resort();

    }

};