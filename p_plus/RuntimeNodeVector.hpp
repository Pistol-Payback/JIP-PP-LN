#pragma once
#include "VectorSortedView.hpp"
#include "NiRuntimeNode.hpp"
#undef min
#undef max
//#include <set>

struct NiRuntimeNodeVector {

    struct NiRuntimeNodeDepthCompare {
        /*
        bool operator()(const NiRuntimeNode& a, const NiRuntimeNode& b) const noexcept
        {
            if (a.cachedPath.size() != b.cachedPath.size())
                return a.cachedPath.size() < b.cachedPath.size();

            return a.cachedPath.raw < b.cachedPath.raw;

        }*/
        bool operator()(const NiRuntimeNode& a, const NiRuntimeNode& b) const noexcept {
            auto adj = [](std::size_t n) -> std::size_t {
                return n ? (n - 1) : std::numeric_limits<std::size_t>::max();
                };
            return adj(a.cachedPath.size()) < adj(b.cachedPath.size());
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

    static inline std::string_view _typeToString(const NiRuntimeNode& node) noexcept {
        if (node.value.isModel()) return "Model";
        if (node.value.isRefID()) return "Ref";
        if (node.value.isLink())  return "^";
        return "Node";
    }

    static inline uint32_t _depthOf(const NiRuntimeNode& n) noexcept {
        // Prefer cached depth if available; else sparse depth (+leaf if present)
        if (n.cachedPath.size() > 0) return n.cachedPath.size();
        return n.sparsePath.size() + (n.node.isValid() ? 1u : 0u);
    }

    inline std::string toString(bool includeHeader = true, bool includeCachedPath = false, bool leadingSlash = false) const
    {

        const size_t count = allPaths.size();
        std::string out;

        // Rough reserve to cut reallocs (tweak multiplier if needed)
        out.reserve((includeHeader ? 32 : 0) + count * 96);

        if (includeHeader) {
            out += "NiRuntimeNodeVector[count=";
            out += std::to_string(count);
            out += "]\n";
        }

        size_t idx = 0;
        for (const NiRuntimeNode* entry : allPaths) {
            if (!entry) continue;

            // [#i T depth=.. mod=..]  sparse\leaf
            out += "[#";
            out += std::to_string(idx++);
            out += ' ';
            out += _typeToString(*entry);
            out += ' ';
            out += "depth=";
            out += std::to_string(_depthOf(*entry));
            out += ' ';
            out += "mod=";
            out += std::to_string(entry->modIndex);
            out += "]  ";

            // sparsePath \ leaf
            out += entry->toString(leadingSlash, /*includeLeaf*/ true);

            // token details (short)
            if (entry->value.isModel()) {
                const auto& modelPath = entry->value.getModelPath();
                out += "  [model:";
                if (modelPath.suffix.isValid()) {
                    out += " *";
                    out += modelPath.suffix.CStr();
                    out += '*';
                }
                out += ' ';
                out += (modelPath.path.isValid() ? modelPath.path.CStr() : "<null>");
                out += ']';
            }
            else if (entry->value.isRefID()) {
                const auto& refModel = entry->value.getRefModel();
                char hexbuf[11];
                std::snprintf(hexbuf, sizeof(hexbuf), "0x%08X", refModel.refID);
                out += "  [ref:";
                out += hexbuf;
                if (refModel.suffix.isValid()) {
                    out += " *";
                    out += refModel.suffix.CStr();
                    out += '*';
                }
                out += ']';
            }
            else if (entry->value.isLink()) {
                out += "  [^parent of ";
                const auto& link = entry->value.getLink();
                out += (link.childObj.isValid() ? link.childObj.CStr() : "<null>");
                out += ']';
            }

            if (includeCachedPath && entry->cachedPath.size() > 0) {
                out += "  | cached=";
                out += entry->cachedPath.toString(leadingSlash);
            }

            out += '\n';
        }

        return out;

    }

    // Copy-append: copies nodes from src into *this
    void appendCopy(const NiRuntimeNodeVector& src) {
        auto srcSpan = src.allPaths.getStorage();           // contiguous storage
        allPaths.reserve(allPaths.size() + srcSpan.size());
        for (auto const& node : srcSpan) {
            allPaths.emplace_back(node);                   // copy-insert unordered
        }
        allPaths.resort();
    }

    void removeWithModIndex(UInt32 ModIndex) {
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return node.modIndex == ModIndex;
            }
        );
    }

    void AppendPathsTo(NVSEArrayVar* varsMap) const noexcept
    {
        if (!varsMap) return;

        pSmallBufferWriter buffer; //We can use the same buffer, because SetElement will internally copy the key.
        for (auto node : allPaths) {

            TESForm* ref = nullptr;
            NiToken::Type type{};
            node->reverseToFormatTo(buffer, ref, type);
            ArrayElementL key(buffer.c_str());
            if (type == NiToken::Type::RefID) {
                SetElement(varsMap, key, ArrayElementL(ref));
            }
            else {
                SetElement(varsMap, key, ArrayElementL(static_cast<double>(type)));
            }
        }
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
            if (existing->cachedPath.containsSparsePath(toFind) || existing->containsSparsePath(prefix, leaf)) {
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
            if (node.cachedPath.containsSparsePath(toFind) || node.containsSparsePath(prefix, leaf)) {
                out.emplace_back(std::move(node));
            }
        }

        // Erase those same ones from allPaths
        /*
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.isValid();
            }
        );
        */
        return out;
    }

    //Returns all the nodes that were removed
    std::vector<NiRuntimeNode> removeNode(const NiBlockPathView& path, const NiFixedString& node) {

        std::vector<NiRuntimeNode> out;
        //if (path.empty()) return out;

        NiRuntimeNode* root = nullptr;
        for (NiRuntimeNode* runtimeNode : allPaths) {

            if (!root) {

                if (runtimeNode->node == node && (path.size() == 0 || runtimeNode->sparsePath == path)) {

                    root = &out.emplace_back(std::move(*runtimeNode));

                    if (runtimeNode->value.isLink()) { //^ parent, we dont remove the child here.
                        break;
                    }

                }

            }
            else {
                // After root was found, remove anything nested under it
                if (runtimeNode->cachedPath.contains(root->cachedPath)) {
                    out.emplace_back(std::move(*runtimeNode));
                }
            }

        }

        // Destroy leftovers
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.isValid();
            }
        );

        return out;
    }

    std::vector<NiRuntimeNode> removeModel(const NiBlockPathView& path, const ModelPath& modelPathToken) {

        std::vector<NiRuntimeNode> out;
        //if (path.empty()) return out;

        NiRuntimeNode* root = nullptr;
        for (NiRuntimeNode* runtimeNode : allPaths) {

            if (!root) {

                if (runtimeNode->value.isModel() && runtimeNode->value.getModelPath() == modelPathToken && runtimeNode->sparsePath == path) {
                    root = &out.emplace_back(std::move(*runtimeNode));
                }

            }
            else {
                // After root was found, remove anything nested under it
                if (runtimeNode->cachedPath.contains(root->cachedPath)) {
                    out.emplace_back(std::move(*runtimeNode));
                }
            }

        }

        // Destroy leftovers
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.isValid();
            }
        );


        return out;
    }

    std::vector<NiRuntimeNode> removeRefModel(const NiBlockPathView& path, const RefModel& modelPathToken) {

        std::vector<NiRuntimeNode> out;
        //if (path.empty()) return out;

        NiRuntimeNode* root = nullptr;
        for (NiRuntimeNode* runtimeNode : allPaths) {

            if (!root) {

                if (runtimeNode->value.isRefID() && runtimeNode->value.getRefModel() == modelPathToken && runtimeNode->sparsePath == path) {
                    root = &out.emplace_back(std::move(*runtimeNode));
                }

            }
            else {
                // After root was found, remove anything nested under it
                if (runtimeNode->cachedPath.contains(root->cachedPath)) {
                    out.emplace_back(std::move(*runtimeNode));
                }
            }

        }

        // Destroy leftovers
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.isValid();
            }
        );


        return out;
    }

    void purgeUncachedNodes() {
        allPaths.remove_if(
            [&](NiRuntimeNode const& node) {
                return !node.cachedPath.size() == 0;
            }
        );
    }

    /// Find a matching NiNodePath, or nullptr if none found
    const NiRuntimeNode* findNodeWithPath(const NiBlockPathBase& toFind) const noexcept {
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
            if (existing.containsSparsePath(prefix, leaf)) {
                return &existing;
            }
        }
        return nullptr;
    }

    bool containsPath(const NiBlockPathStatic& toFind) {
        const auto& storage = allPaths.getStorage();
        for (auto const& node : storage) {
            if (node.cachedPath.contains(toFind))
                return true;
        }
    }

    NiRuntimeNode* lookupNode(const NiBlockPathView& path, const NiFixedString& node) noexcept {
        for (auto& existing : allPaths.getStorage()) {
            if (existing.node == node && existing.sparsePath == path)
                return &existing;
        }
        return nullptr;
    }

    NiRuntimeNode* lookupModel(const NiBlockPathView& path, const ModelPath& modelPathToken) noexcept {
        for (auto& existing : allPaths.getStorage()) {
            if (existing.value.isModel() && existing.value.getModelPath() == modelPathToken && existing.sparsePath == path)
                return &existing;
        }
        return nullptr;
    }

    NiRuntimeNode* lookupRefModel(const NiBlockPathView& path, const RefModel& modelPathToken) noexcept {
        for (auto& existing : allPaths.getStorage()) {
            if (existing.value.isRefID() && existing.value.getRefModel() == modelPathToken && existing.sparsePath == path)
                return &existing;
        }
        return nullptr;
    }

    bool emplaceParentNode(UInt32 modIndex, NiBlockPathView& childPath, const NiFixedString& node, NiBlockPathBuilder&& fullPath) {
        if (lookupNode(childPath, node)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(childPath), node, NiToken(IndexLinkedList(childPath.back())), fullPath.toStaticPath());
        return true;
    }

    bool emplaceParentNode(UInt32 modIndex, NiBlockPathView& childPath, const NiFixedString& node) {
        if (lookupNode(childPath, node)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(childPath), node, NiToken(IndexLinkedList(childPath.back())), NiBlockPathStatic());
        return true;
    }

    bool emplaceChildNode(UInt32 modIndex, NiBlockPathView& parentPath, const NiFixedString& node, NiBlockPathBuilder&& fullPath) {
        if (lookupNode(parentPath, node)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), node, NiToken(), fullPath.toStaticPath());
        return true;
    }

    bool emplaceChildNode(UInt32 modIndex, NiBlockPathView& parentPath, const NiFixedString& node) {
        if (lookupNode(parentPath, node)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), node, NiToken(), NiBlockPathStatic());
        return true;
    }

    bool emplaceModel(UInt32 modIndex, NiBlockPathView& parentPath, ModelPath& modelPath, const NiFixedString& modelRootNode, NiBlockPathBuilder&& fullPath) {
        if (lookupModel(parentPath, modelPath)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), modelRootNode, NiToken(modelPath), fullPath.toStaticPath());
        return true;
    }

    bool emplaceModel(UInt32 modIndex, NiBlockPathView& parentPath, ModelPath& modelPath) {
        if (lookupModel(parentPath, modelPath)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), NiFixedString(), NiToken(modelPath), NiBlockPathStatic());
        return true;
    }

    bool emplaceRefModel(UInt32 modIndex, NiBlockPathView& parentPath, RefModel& refModel, const NiFixedString& modelRootNode, NiBlockPathBuilder&& fullPath) {
        if (lookupRefModel(parentPath, refModel)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), modelRootNode, NiToken(refModel), fullPath.toStaticPath());
        return true;
    }

    bool emplaceRefModel(UInt32 modIndex, NiBlockPathView& parentPath, RefModel& refModel) {
        if (lookupRefModel(parentPath, refModel)) { return false; }
        allPaths.emplace(modIndex, NiBlockPathBase::createCopy(parentPath), NiFixedString(), NiToken(refModel), NiBlockPathStatic());
        return true;
    }

    bool attachModel(UInt32 modIndex, NiNode* root, NiBlockPathView& attachPath, ModelPath& modelPath, bool insertIntoMap) {

        if (!root) {
            if (!insertIntoMap) {
                return false;
            }
            return emplaceModel(modIndex, attachPath, modelPath);
        }

        NiBlockPathBuilder builderPath;
        NiNode* node = (NiNode*)root->BuildNiPath(attachPath, builderPath);
        if (!node || !node->isNiNode()) {
            return false;
        }

        ModelTemp tempModel = ModelTemp(modelPath.path.getPtr());

        if (!tempModel.clonedNode) {
            return false; //Model failed to load
        }

        if (!node->hasChildNode(tempModel.clonedNode->m_blockName)) {

            NiNode* toAttach = tempModel.detachNode();

            toAttach->RemoveCollision(); //Prevents invalid collision
            toAttach->AddSuffixToAllChildren(modelPath.suffix.getPtr());
            toAttach->DownwardsInitPointLights(toAttach);
            toAttach->AddPointLights();
            node->attachRuntimeNode(toAttach);
            node->UpdateTransformAndBounds(kNiUpdateData);
            node->updatePalette();

            if (insertIntoMap) {
                return emplaceModel(modIndex, attachPath, modelPath, toAttach->m_blockName, std::move(builderPath));
            }

        }

        return false;

    }

    bool attachRefModel(UInt32 modIndex, NiNode* root, NiBlockPathView& attachPath, RefModel& refModel, ModelTemp& formModel, bool insertIntoMap) {

        if (!root) {
            if (!insertIntoMap) {
                return false;
            }
            return emplaceRefModel(modIndex, attachPath, refModel);
        }

        NiBlockPathBuilder builderPath;
        NiNode* node = (NiNode*)root->BuildNiPath(attachPath, builderPath);
        if (!node || !node->isNiNode()) {
            return false;
        }

        if (!node->hasChildNode(formModel.clonedNode->m_blockName)) {

            NiNode* toAttach = formModel.detachCopyNode();

            node->attachRuntimeNode(toAttach);
            node->UpdateTransformAndBounds(kNiUpdateData);
            node->updatePalette();

            if (insertIntoMap) {
                return emplaceRefModel(modIndex, attachPath, refModel, toAttach->m_blockName, std::move(builderPath));
            }

        }

        return false;

    }

    bool attachNode(UInt32 modIndex, NiNode* root, NiBlockPathView& attachPath, const char* nodeName, bool insertIntoMap) {

        if (!nodeName) return false;

        //Insert into map without attaching to the model
        if (!root) {

            if (!insertIntoMap) {
                return false;
            }

            if (nodeName[0] == '^') {
                //Skip the ^ parent syntax
                return emplaceParentNode(modIndex, attachPath, NiFixedString(nodeName + 1));
            }
            else {
                return emplaceChildNode(modIndex, attachPath, NiFixedString(nodeName));
            }
        }

        //Attach to live root

        NiBlockPathBuilder fullPath;
        NiNode* target = static_cast<NiNode*>(root->BuildNiPath(attachPath, fullPath));

        if (!target) return false;

        if (nodeName[0] == '^') {

            NiFixedString name(nodeName + 1); //Skip the ^ parent syntax
            if (target == root || !target->m_parent || target->m_parent->m_blockName == name) return false;

            // Splice all existing NiRuntimeNode paths
            updatePathsOnNodeInsert(fullPath, name);
            NiNode* newParentNode = NiNode::nCreate(name);

            // Splice into the scene graph
            NiNode* grandparent = target->m_parent;

            // re‑parent the original node under our newParent
            newParentNode->AddObject(target, true);
            grandparent->attachRuntimeNode(newParentNode);

            grandparent->UpdateTransformAndBounds(kNiUpdateData);
            grandparent->updatePalette();

            // use the parent path
            fullPath.pop();

            if (insertIntoMap) {
                return emplaceParentNode(modIndex, attachPath, name, std::move(fullPath));
            }

        }
        else {

            NiFixedString name(nodeName);
            if (!target || !target->isNiNode() || target->hasChildNode(name))
                return false;

            NiNode* newChildNode = NiNode::nCreate(name);
            if (!newChildNode) return false;

            target->attachRuntimeNode(newChildNode);
            target->UpdateTransformAndBounds(kNiUpdateData);
            target->updatePalette();

            if (insertIntoMap) {
                return emplaceChildNode(modIndex, attachPath, name, std::move(fullPath));
            }

        }

        return true;

    }

    bool empty() const noexcept {
        return allPaths.empty();
    }

    void updatePathsOnNodeInsert(const NiBlockPathBuilder& parentPath, const NiFixedString& segName) {
        const size_t insertAt = parentPath.size() - 1;
        for (auto& node : allPaths.getStorage()) {
            if (node.cachedPath.contains(parentPath)) {
                node.cachedPath.insertSegment(insertAt, segName);
            }
        }
        allPaths.resort();
    }

    void updatePathsOnNodeRename(const NiBlockPathStatic& parentPath, const NiFixedString& oldName, const NiFixedString& newName) {

        const size_t depth = parentPath.size() - 1;

        for (auto& node : allPaths.getStorage()) {
            if (node.cachedPath.contains(parentPath) &&
                node.cachedPath[depth] == oldName)
            {
                node.cachedPath.replaceSegment(depth, newName);
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
            if (node.cachedPath.contains(parentPath)) {
                node.cachedPath.removeSegment(removeAt);
            }
            // also fix the leaf name if it was the removed node:
            if (node.node == nodeToRemove) {
                node.node = NiFixedString();
            }
        }
        allPaths.resort();

    }

    /*
                    toRemove = (NiNode*)root->DeepSearchByPath(NiBlockPathBase(entry->cachedPath, entry->node));
                if (toRemove && toRemove->m_blockName == entry->cachedPath.back()) { //Found root path, but not the runtime node.
                    continue; //Runtime node isn't attached, skip.
                }

                if (!toRemove || toRemove->m_blockName != entry->node) { //Didin't find full path, lets rebuild the cache

                    NiNode* subSearch = root;
                    if (toRemove && toRemove->isNiNode()) {
                        subSearch = (NiNode*)toRemove; //Continue where search by path left off.
                    }

                    NiBlockPathBuilder builderPath;
                    NiNode* node = (NiNode*)root->BuildNiPath(entry->sparsePath, builderPath); //Rebuild path
                    if (!node || !node->isNiNode()) {
                        entry->cachedPath.clear(); //Bad runtime node, mark for delete
                        update = true;
                        continue;
                    }

                    if (entry->cachedPath != builderPath) {
                        entry->cachedPath = std::move(builderPath.toStaticPath()); //Update cache
                        update = true;
                    }

                    toRemove = node->DeepSearchBySparsePath(NiBlockPathView(&entry->node, 1));

                }
    */

    NiAVObject* findAndUpdateNode(NiRuntimeNode& runtimeNode, NiNode& root, bool& updated) {

        uint16_t resultDepth;
        NiBlockPathBase fullPath = NiBlockPathBase(runtimeNode.cachedPath, runtimeNode.node);
        NiAVObject* found = root.DeepSearchByPath(fullPath, resultDepth);

        if (!found || (runtimeNode.node != found->m_blockName) || resultDepth <= fullPath.size()) { //Didn't find full path, lets rebuild the cache

            NiNode* subSearch = &root;
            NiBlockPathBuilder builderPath;

            if (runtimeNode.value.isLink()) { //parent node

                NiBlockPathBuilder builderPath;
                NiNode* child = (NiNode*)subSearch->BuildNiPath(runtimeNode.sparsePath, builderPath);
                if (!child || child == &root) {
                    runtimeNode.cachedPath.clear(); //Bad runtime node, mark for delete
                    updated = true;
                    return nullptr;
                }

                builderPath.pop(); //Use grandparent
                runtimeNode.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

                return child->findParentNode(runtimeNode.node, &root);

            }
            else { //Normal node
                NiNode* node = (NiNode*)subSearch->BuildNiPath(runtimeNode.sparsePath, builderPath); //Rebuild path
                if (!node || !node->isNiNode()) {
                    runtimeNode.cachedPath.clear(); //Bad runtime node, mark for delete
                    updated = true;
                    //Console_Print("JIP::findAndUpdateNode bad runtime node, cachedPath is invalid %s", runtimeNode.originToDebugString().c_str());
                    return nullptr;
                }

                if (runtimeNode.cachedPath != builderPath) {
                    runtimeNode.cachedPath = std::move(builderPath.toStaticPath()); //Update cache
                    updated = true;
                }

                return node->DeepSearchBySparsePath(NiBlockPathView(&runtimeNode.node, 1));
            }

        }

        return found;

    }

    //Also does some cleanup, update this to use the node insert flags.
    bool removeAllRuntimeNodes(NiNode* root) {

        if (!root) { return false; }

        bool update = false;
        for (NiRuntimeNode* entry : std::views::reverse(allPaths)) { //Iterate backwards so we dont remove any nodes before they're found

            if (entry->isValid() == false) { //Skip nodes without cached path
                continue;
            }
            else if (NiAVObject* toRemove = findAndUpdateNode(*entry, *root, update)) {

                if (entry->value.isLink()) { //Is a ^parent inserted node

                    NiNode* insertedParent = static_cast<NiNode*>(toRemove);
                    const NiFixedString* childName = &entry->value.getLink().childObj;

                    NiBlockPathBuilder builder;

                    if (NiAVObject* originalChild = insertedParent->findFirstValidChild()) {
                        insertedParent->replaceMe(originalChild); //Swaps insertedParent with originalChild
                    }
                    else {
                        Console_Print("JIP::removeAllRuntimeNodes error, couldn't find a valid child to replace an inserted parent: %s", entry->originToDebugString().c_str());
                        insertedParent->m_parent->RemoveObject(insertedParent);
                    }


                }
                else {
                    auto parent = toRemove->m_parent;
                    parent->RemoveObject(toRemove);
                    //parent->UpdateTransformAndBounds(kNiUpdateData);
                }

            }

        }

        if (update) {
            allPaths.resort(); //This should push all the empty cachedPaths to the end of the path stack.
            return false; //has invalid nodes
        }

        return true;

    }

    //Called from refreshRuntimeNodes
    bool attachAllRuntimeNodes(NiNode* root) {

        if (!root) {
            return false;
        }

        bool allValid = true;
        //Attachments should already be in order
        for (NiRuntimeNode* entry : this->allPaths) {
            if (entry->value.isModel()) {
                if (this->attachRuntimeModel(*root, *entry) == false) {
                    allValid = false;
                }
            }
            else if (entry->value.isRefID()) {
                this->attachRuntimeRef(*root, *entry);
            }
            else if (entry->value.isLink()) {
                if (attachRuntimeNodeParent(*root, *entry) == false) {
                    allValid = false;
                }
            }
            else if (attachRuntimeNodeChild(*root, *entry) == false) {
                allValid = false;
            }
        }

        return allValid;

    }

    //Attaching node from attachRuntimeNodes
    bool attachRuntimeModel(NiNode& root, NiRuntimeNode& toAttach) {

        const ModelPath& modelPath = toAttach.value.getModelPath();

        if (!modelPathExists(modelPath.path.CStr())) {
            toAttach.cachedPath.clear(); //Invalidate cache so its removed
            return false;
        }

        NiNode* node = nullptr;
        if (toAttach.cachedPath.size() == 0) { //Create cached path

            NiBlockPathBuilder builderPath;
            node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
            if (!node || !node->isNiNode()) {
                return false;
            }
            toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

        }
        else {

            uint16_t resultDepth;
            node = (NiNode*)root.DeepSearchByPath(toAttach.cachedPath, resultDepth);
            //if (!resultDepth) {
                 //Console_Print("JIP::attachRuntimeModel error, cachedPath is invalid: %s", toAttach.originToDebugString().c_str());
                 //return false;
             //}

            if (resultDepth != toAttach.cachedPath.size()) { //Didin't find full path

                NiBlockPathBuilder builderPath;
                node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
                if (!node || !node->isNiNode()) {
                    toAttach.cachedPath.clear(); //Invalidate cache
                    return false;
                }
                toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache
            }

        }

        ModelTemp tempModel = ModelTemp(modelPath.path.getPtr());

        if (!tempModel.clonedNode) {
            return true; //Model failed to load
        }

        if (!node->hasChildNode(tempModel.clonedNode->m_blockName)) {

            NiNode* formRootNode = tempModel.detachNode();

            formRootNode->RemoveCollision(); //Prevents invalid collision
            formRootNode->AddSuffixToAllChildren(modelPath.suffix.getPtr());
            formRootNode->DownwardsInitPointLights(formRootNode);
            formRootNode->AddPointLights();

            node->attachRuntimeNode(formRootNode);
            //node->UpdateTransformAndBounds(kNiUpdateData);

            if (!toAttach.node.isValid()) { //Node can be null if model wasn't instantly loaded and attached when call from a script.
                toAttach.node = formRootNode->m_blockName;
            }

        }

        return true;

    }

    bool attachRuntimeNodeChild(NiNode& root, NiRuntimeNode& toAttach) {

        NiNode* node = nullptr;
        if (toAttach.cachedPath.size() == 0) { //Create cached path

            NiBlockPathBuilder builderPath;
            node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
            if (!node || !node->isNiNode()) {
                return false;
            }
            toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

        }
        else {

            uint16_t resultDepth;
            node = (NiNode*)root.DeepSearchByPath(toAttach.cachedPath, resultDepth);
            //if (!resultDepth) {
                //Console_Print("JIP::attachRuntimeNodeChild error, cachedPath is invalid: %s", toAttach.originToDebugString().c_str());
                //return false;
           // }

            if (resultDepth != toAttach.cachedPath.size()) { //Didin't find full path

                NiBlockPathBuilder builderPath;
                node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);

                if (!node || !node->isNiNode()) {
                    toAttach.cachedPath.clear(); //Invalidate cache
                    return false;
                }
                toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

            }

        }

        if (node->hasChildNode(toAttach.node)) {
            return true;
        }

        NiNode* newNode = NiNode::nCreate(toAttach.node);
        node->attachRuntimeNode(newNode);
        //node->UpdateTransformAndBounds(kNiUpdateData);

        newNode->m_flags.set(NiAVObject::NiFlags::kNiFlag_IsInserted);

        return true;

    }

    bool attachRuntimeNodeParent(NiNode& root, NiRuntimeNode& toAttach) {

        NiNode* child = nullptr;
        NiNode* grandparent = nullptr;
        if (toAttach.cachedPath.size() == 0) { //Create cached path

            NiBlockPathBuilder builderPath;
            child = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
            if (!child || child == &root) {
                return false;
            }

            grandparent = child->m_parent;
            builderPath.pop(); //Use grandparent
            toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

        }
        else {

            uint16_t resultDepth;
            grandparent = (NiNode*)root.DeepSearchByPath(toAttach.cachedPath, resultDepth);

            //if (!resultDepth) {
                //Console_Print("JIP::attachRuntimeNodeParent error, cachedPath is invalid %s", toAttach.originToDebugString().c_str());
                //return false;
           // }

            if (resultDepth != toAttach.cachedPath.size()) { //Didin't find full path

                NiBlockPathBuilder builderPath;
                child = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
                if (!child || child == &root) {
                    toAttach.cachedPath.clear(); //Invalidate cache
                    return false;
                }

                grandparent = child->m_parent;
                builderPath.pop();
                toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache
            }
            else {
                child = (NiNode*)grandparent->DeepSearchBySparsePath(toAttach.sparsePath);
                grandparent = child->m_parent;
            }

        }

        if (!child) { //Child node was probably removed.
            toAttach.cachedPath.clear(); //Invalidate cache
            return false;
        }

        if (child->findParentNode(toAttach.node, &root)) {
            return true;
        }

        NiNode* newParent = NiNode::nCreate(toAttach.node);
        newParent->AddObject(child, true);
        grandparent->attachRuntimeNode(newParent);

        return true;

    }

    bool attachRuntimeRef(NiNode& root, NiRuntimeNode& toAttach) {

        const RefModel& modelPath = toAttach.value.getRefModel();

        NiRuntimeNodeVector* refRuntimeNodes = nullptr;
        ModelTemp tempModel = modelPath.getRuntimeModel(refRuntimeNodes);
        if (tempModel.hasFilePathReadyForLoad() == false) {
            return false; //Model failed to load
        }

        NiNode* node = nullptr;
        if (toAttach.cachedPath.size() == 0) { //Create cached path

            NiBlockPathBuilder builderPath;
            node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
            if (!node || !node->isNiNode()) {
                return false;
            }
            toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache

        }
        else {

            uint16_t resultDepth;
            node = (NiNode*)root.DeepSearchByPath(toAttach.cachedPath, resultDepth);
            if (!resultDepth) {
                Console_Print("attachRuntimeNodeChild JIP Error, cachedPath is invalid");
                return false;
            }

            if (resultDepth != toAttach.cachedPath.size()) { //Didin't find full path

                NiBlockPathBuilder builderPath;
                node = (NiNode*)root.BuildNiPath(toAttach.sparsePath, builderPath);
                if (!node || !node->isNiNode()) {
                    toAttach.cachedPath.clear(); //Invalidate cache
                    return false;
                }
                toAttach.cachedPath = std::move(builderPath.toStaticPath()); //Update cache
            }

        }

        tempModel.load();
        if (!tempModel.clonedNode) {
            return true; //Model failed to load
        }

        if (!node->hasChildNode(tempModel.clonedNode->m_blockName)) {

            NiNode* toAttachModel = tempModel.detachNode();

            toAttachModel->attachAllRuntimeNodes(refRuntimeNodes);
            toAttachModel->RemoveCollision(); //Prevents invalid collision
            toAttachModel->AddSuffixToAllChildren(modelPath.suffix.getPtr());
            toAttachModel->DownwardsInitPointLights(toAttachModel);
            toAttachModel->AddPointLights();

            node->attachRuntimeNode(toAttachModel);

            if (!toAttach.node.isValid()) { //Node can be null if model wasn't instantly loaded and attached when call from a script.
                toAttach.node = toAttachModel->m_blockName;
            }

        }

        return true;

    }

};