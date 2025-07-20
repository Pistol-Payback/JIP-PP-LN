#pragma once
#include "NiRuntimeNode.hpp"

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
    /*
    void buildAndMovePath(NiRuntimeNode& result, const NiFixedString& node, const NiToken& value = {}) noexcept {
        // Move the segments into the new heap buffer

        if (empty()) {
            return;
        }

        result.node = node;
        result.value = value;
        //result.path = nullptr;
        result.path = NiBlockPath{ std::make_move_iterator(parents.begin()), std::make_move_iterator(parents.begin() + length) };

    }*/

    NiRuntimeNode buildAndMovePath(const NiFixedString& node, const NiToken& value = {}) {
        // Move the segments into the new heap buffer

        if (empty()) {
            return NiRuntimeNode();
        }

        return NiRuntimeNode(
            NiBlockPath{ parents.begin(), parents.begin() + length },
            node,
            value
        );
    }

};

//----------------------------------------------------------------------------
// Custom “attached” cache wrapper: manages model‐attachment registrations.
//----------------------------------------------------------------------------
struct ModelBuilder {

    NiNode* model;

    ModelBuilder(NiNode* rootNode, const NiRuntimeNodeVector* runtimeNodes) : model(rootNode) {
        if (!model) return;
        attachRuntimeNodes(runtimeNodes);
    }

    //Check for dupe nodes first
    NiRuntimeNode attachNewModel(const char* attachPath, const char* modelPath, const char* suffix) {

        if (!modelPath || !modelPathExists(modelPath)) {
            return NiRuntimeNode();
        }

        NiBlockPathBuilder outPath;
        NiNode* node = (NiNode*)model->BuildNiPath(attachPath, outPath);

        ModelTemp tempModel = ModelTemp(modelPath);
        if (node && node->isNiNode() && !node->hasChildNode(tempModel.clonedNode->m_blockName)) {

            NiNode* toAttach = tempModel.detachNode();

            toAttach->RemoveCollision(); //Prevents invalid collision
            toAttach->AddSuffixToAllChildren(suffix);
            toAttach->DownwardsInitPointLights(model);
            toAttach->AddPointLights();

            node->AddObject(toAttach, true);
            node->UpdateTransformAndBounds(kNiUpdateData);
            return outPath.buildAndMovePath(toAttach->m_blockName, NiToken(modelPath, suffix)); //Pass model path as NiRuntimeNode value

        }

        return NiRuntimeNode();

    }

    //Check for dupe nodes first
    NiRuntimeNode attachNewNode(const char* attachPath, const char* nodeName) {

        NiBlockPathBuilder outPath;
        NiNode* node = (NiNode*)model->BuildNiPath(attachPath, outPath);

        if (node && node->isNiNode() && !node->hasChildNode(nodeName)) {

            NiNode* newNode = NiNode::pCreate(nodeName);
            node->AddObject(newNode, true);
            node->UpdateTransformAndBounds(kNiUpdateData);
            return outPath.buildAndMovePath(newNode->m_blockName);

        }

        return NiRuntimeNode();

    }

    static void checkRuntimeNodes(NiNode* rootnode, const NiRuntimeNodeVector* runtimeNodes) {

        //Attachments should already be in order
        if (runtimeNodes && rootnode) {
            for (const NiRuntimeNode* entry : runtimeNodes->allPaths) {
                if (entry->value.isModel()) {
                    if (!(rootnode->DeepSearchByName(entry->node))) {
                        Console_Print("Error attachRuntimeNodes");
                    }
                }
                else if (entry->value.isRefID()) {
                    if (!(rootnode->DeepSearchByName(entry->node))) {
                        Console_Print("Error attachRuntimeNodes");
                    }
                }
                else {
                    if (!(rootnode->DeepSearchByName(entry->node))) {
                        Console_Print("Error attachRuntimeNodes");
                    }
                }

            }
        }

    }

private:

    void attachRuntimeNodes(const NiRuntimeNodeVector* runtimeNodes) {

        //Attachments should already be in order
        if (runtimeNodes) {
            for (const NiRuntimeNode* entry : runtimeNodes->allPaths) {
                if (entry->value.isModel()) {
                    attachModel(*entry);
                }
                else if (entry->value.isRefID()) {
                    //attachRef(*entry);
                }
                else {
                    attachNode(*entry);
                }

            }

            //checkRuntimeNodes(model, runtimeNodes);
        }

    }

    void attachRef(const NiRuntimeNode& toAttach) {

        NiNode* node = (NiNode*)model->DeepSearchByPath(toAttach.path); //Check if it has the node already
        if (!node) {
            return;
        }
        /*
        const RefModel& refModel = toAttach.value.getRefModel();
        if (TESForm* form = LookupFormByRefID(refModel.refID)) {

            if (form->typeID == kFormType_TESObjectLIGH) {
                //Do light attach
            }

            NiNode* childModel = LoadModelCopy(form->GetModelPath());

            if (node->isNiNode() && childModel) {

                childModel->AddSuffixToAllChildren(refModel.suffix.getPtr());
                node->AddObject(childModel, true);
                node->UpdateTransformAndBounds(kNiUpdateData);

            }

        }*/
    }

    //Attaching node from attachRuntimeNodes
    void attachModel(const NiRuntimeNode& toAttach) {

        const ModelPath& modelPath = toAttach.value.getModelPath();
        if (!modelPathExists(modelPath.path.CStr())) {
            return;
        }

        NiNode* node = (NiNode*)model->DeepSearchByPath(toAttach.path);

        if (node && node->isNiNode() && !node->hasChildNode(toAttach.node)) {

            ModelTemp tempModel = ModelTemp(modelPath.path.getPtr());
            if (node && node->isNiNode() && !node->hasChildNode(tempModel.clonedNode->m_blockName)) {

                NiNode* toAttach = tempModel.detachNode();

                toAttach->RemoveCollision(); //Prevents invalid collision
                toAttach->AddSuffixToAllChildren(modelPath.suffix.getPtr());
                toAttach->DownwardsInitPointLights(model);
                toAttach->AddPointLights();

                node->AddObject(toAttach, true);
                node->UpdateTransformAndBounds(kNiUpdateData);

            }

        }

    }

    void attachNode(const NiRuntimeNode& toAttach) {

        NiNode* node = (NiNode*)model->DeepSearchByPath(toAttach.path);

        if (node && node->isNiNode() && !node->hasChildNode(toAttach.node)) {

            NiNode* newNode = NiNode::pCreate(toAttach.node);
            newNode->m_flags = NiAVObject::NiFlags::kNiFlag_IsInserted;
            node->AddObject(newNode, true);
            node->UpdateTransformAndBounds(kNiUpdateData);

        }

    }

    bool modelPathExists(const char* path) {
        static char meshesPath[0x100] = "data\\meshes\\";
        StrCopy(meshesPath + 12, path);
        if (!FileExistsEx(meshesPath, false)) {
            return false;
        }
        return true;
    }

};