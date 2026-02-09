#pragma once
#include "RuntimeNodeVector.hpp"
#include "NiBlockPath.hpp"
#include "NiBlockPathBuilder.hpp"
#include "pStringUtilities.hpp"
#include <unordered_map>

enum AttachHookFlags : uint8_t
{
    kAttachHookFlags_InsertNode = 64,       //kAttachHookFlags_InsertNode
    kAttachHookFlags_AttachModel = 128,     //kAttachHookFlags_AttachModel
};


class BaseRuntimeModelManager {

    std::unordered_map<const char*, NiRuntimeNodeVector> modelBuilder = {};

    BaseRuntimeModelManager() = default;
    ~BaseRuntimeModelManager() = default;

    static BaseRuntimeModelManager s_nodeManager;

    NiRuntimeNodeVector* getNodesList(const char* modelPath) {
        const auto iter = modelBuilder.find(modelPath);
        NiRuntimeNodeVector* pathList = nullptr;
        if (iter != modelBuilder.end()) {
            pathList = &iter->second;
        }
        return pathList;
    };
    const NiRuntimeNodeVector* getNodesList(const char* modelPath) const {
        const auto iter = modelBuilder.find(modelPath);
        const NiRuntimeNodeVector* pathList = nullptr;
        if (iter != modelBuilder.end()) {
            pathList = &iter->second;
        }
        return pathList;
    };

    /// Register a new node‐path for `form`.
    /// If this is the first path for the form, set its JIP flag on.
    bool RegisterNode(UInt32 modIndex, const char* modelPath, const char* formattedNode) {

        NiRuntimeNodeVector* runtimeList = getNodesList(modelPath);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(const_cast<char*>(formattedNode), outPath, outNodeName, outSuffix);

        NiBlockPathBase blockPath(outPath);

        // skip a leading '^' if present
        const char* name = outNodeName + (outNodeName[0] == '^');
        if (runtimeList) {
            if (runtimeList->lookupNode(blockPath, name)) {
                return false;  // Already attached
            }
        }
        Model* model;
        if (!g_modelLoader->loadModel(modelPath, model)) {
            return false; //Model failed to load
        }

        if (!runtimeList) {
            // first-ever node for this form
            NiRuntimeNodeVector modelRuntimeNodes;
            if (!modelRuntimeNodes.attachNode(modIndex, model->niNode, blockPath, outNodeName, true)) {
                return false;
            }
            auto result = modelBuilder.try_emplace(modelPath, std::move(modelRuntimeNodes));
            return result.second;
        }
        else if (!runtimeList->attachNode(modIndex, model->niNode, blockPath, outNodeName, true)) {
            return false;
        }

        return true;

    }

    bool RegisterModel(UInt32 modIndex, const char* modelPath, char* formattedNode) {

        NiRuntimeNodeVector* runtimeList = getNodesList(modelPath);

        const char* outPath = nullptr;
        const char* toAttachModelPath = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, toAttachModelPath, outSuffix);

        if (!modelPath || !modelPathExists(toAttachModelPath) || !modelPathExists(modelPath)) {
            return false;
        }

        NiBlockPathBase blockPath(outPath);
        ModelPath attachModel(modelPath, outSuffix);

        // Already attached?  Nothing to do.
        if (runtimeList && runtimeList->lookupModel(blockPath, attachModel)) {
            return false;
        }

        Model* model;
        if (!g_modelLoader->loadModel(modelPath, model)) {
            return false; //Model failed to load
        }

        if (!runtimeList) {

            NiRuntimeNodeVector modelRuntimeNodes;
            if (!modelRuntimeNodes.attachModel(modIndex, model->niNode, blockPath, attachModel, true)) {
                return false;
            }
            auto result = modelBuilder.try_emplace(modelPath, std::move(modelRuntimeNodes));
            return result.second;
        }
        else {

            if (!runtimeList->attachModel(modIndex, model->niNode, blockPath, attachModel, true)) {
                return false;
            }

        }

        return true;

    }

    bool RegisterRefModel(UInt32 modIndex, const char* modelPath, TESForm* toAttach, char* formattedNode) {

        if (!toAttach) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(modelPath);

        const char* outPath = nullptr;
        const char* outSuffix = nullptr;
        const char* nop = nullptr; //NotUsed
        pUtils::formatString(formattedNode, outPath, nop, outSuffix);
        if (!outPath) {
            outPath = nop;
        }

        RefModel refModel(toAttach->refID, outSuffix);
        NiBlockPathBase blockPath(outPath);

        // Already attached?  Nothing to do.
        if (runtimeList && runtimeList->lookupRefModel(blockPath, refModel)) {
            return false;
        }

        ModelTemp formModel = refModel.getRuntimeModel(toAttach);
        if (!formModel.isValid()) {
            return false;
        }

        Model* model;
        if (!g_modelLoader->loadModel(modelPath, model)) {
            return false; //Model failed to load
        }

        if (!runtimeList) {

            NiRuntimeNodeVector modelRuntimeNodes;
            if (!modelRuntimeNodes.attachRefModel(modIndex, model->niNode, blockPath, refModel, formModel, true)) {
                return false;
            }
            auto result = modelBuilder.try_emplace(modelPath, std::move(modelRuntimeNodes));
            return result.second;

        }
        else {

            if (!runtimeList->attachRefModel(modIndex, model->niNode, blockPath, refModel, formModel, true)) {
                return false;
            }

        }

        return true;

    }

};

//----------------------------------------------------------------------
// Custom “inserted” cache wrapper: manages node‐insertion registrations.
//----------------------------------------------------------------------
class FormRuntimeModelManager
{

    // map from form → vector of NiNodePath
    std::unordered_map<TESForm*, NiRuntimeNodeVector> nodeTree = {};

    NiRuntimeNodeVector playerFirstPersonModel;

    FormRuntimeModelManager() = default;
    ~FormRuntimeModelManager() = default;

    static FormRuntimeModelManager s_nodeManager;

public:

    NiRuntimeNodeVector& getFirstPersonModelList() {
        return playerFirstPersonModel;
    }

    static FormRuntimeModelManager& getSingleton() {
        return s_nodeManager;
    }

    bool CopyRuntimeNodes(TESForm* copyTo, TESForm* copyFrom) noexcept {

        if (!copyFrom || !copyTo) return false;
        if (!copyFrom->IsBoundObject() || !copyTo->IsBoundObject()) return false;

        NiRuntimeNodeVector* source = getNodesList(copyFrom);
        if (!source) return false;

        if (NiRuntimeNodeVector* destination = getNodesList(copyTo)) {
            destination->appendCopy(*source);
        }
        else {
            // No destination yet: create empty, then append
            nodeTree.try_emplace(copyTo, *source);
            copyTo->SetJIPFlag(kAttachHookFlags_InsertNode, true);
        }

        return true;
    }

    bool OverwriteRuntimeNodes(TESForm* copyTo, TESForm* copyFrom) {

        if (!copyFrom || !copyTo) return false;
        if (!copyFrom->IsBoundObject() || !copyTo->IsBoundObject()) return false;

        NiRuntimeNodeVector* source = getNodesList(copyFrom);
        if (!source) return false;
        auto [it, inserted] = nodeTree.insert_or_assign(copyTo, *source);
        if (inserted) {
            copyTo->SetJIPFlag(kAttachHookFlags_InsertNode, true);
        }
        return true;
    }

    bool RemoveAllRuntimeNodes(TESForm* form) {

        if (!form) return false;
        if (!form->IsBoundObject()) return false;
        nodeTree.erase(form);
        return true;
    }

    bool RemoveAllRuntimeNodes_RelToMod(TESForm* form, UInt32 modIndex) {

        if (!form) return false;
        if (!form->IsBoundObject()) return false;

        NiRuntimeNodeVector* source = getNodesList(form);
        if (!source) return false;
        source->removeWithModIndex(modIndex);

        return true;
    }

    // Takes a baseform
    bool HasNode(TESForm* form, const char* fullPath, bool searchFullModel = true) noexcept {

        if (!form || !fullPath || !*fullPath)
            return false;

        return HasNode(form, NiBlockPathBase(fullPath), searchFullModel);
    }

    // Returns true if any registered NiNodePath for this form matches `toFind`
    // Takes a baseform
    bool HasNode(TESForm* form, const NiBlockPathBase& pathToFind, bool searchFullModel = true) noexcept {

        if (!form)
            return false;

        // Check player first-person if needed
        if (form->IsPlayer()) {

            if (playerFirstPersonModel.findNodeWithPath(pathToFind)) {
                return true;
            }

            if (g_thePlayer->node1stPerson && g_thePlayer->node1stPerson->DeepSearchBySparsePath(pathToFind))
                return true;

        }
        else if (!form->IsBoundObject()) {
            return false;
        }

        // Check our registered cache using buf (prefix) + leafName
        NiRuntimeNodeVector* runtimeList = getNodesList(form);
        if (runtimeList) {
            if (runtimeList->findNodeWithPath(pathToFind))
                return true;
        }

        if (searchFullModel == false) {
            return false;
        }

        // Fallback: fully built model search
        if (const char* path = form->GetModelPath()) {

            ModelTemp temp = ModelTemp(path);
            if (!temp.clonedNode) { //Model failed to load
                return false;
            }

            temp.clonedNode->attachAllRuntimeNodes(runtimeList);

            if (NiNode* liveRoot = temp.clonedNode) {
                if (liveRoot->DeepSearchBySparsePath(pathToFind)) {
                    return true;
                }

            }
        }

        return false;

    }

    bool hasRuntimeNodePath(TESForm* form, const char* fullPath) {

        if (!form) return false;

        NiBlockPathBase blockPath(fullPath);

        // First-person hook (Player only)
        if (form->IsPlayer() && playerFirstPersonModel.findNodeWithPath(blockPath) == nullptr) {
            return true;
        }
        else if (!form->IsBoundObject()) {
            return false;
        }

        if (NiRuntimeNodeVector* runtimeList = getNodesList(form)) {
            if (runtimeList->findNodeWithPath(blockPath))
                return true;
        }
        return false;
    }

    bool hasRuntimeNodeNiNode(TESForm* form, char* formattedNode) {

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, outNodeName, outSuffix);

        NiBlockPathBase blockPath(outPath);

        // skip a leading '^' if present
        const char* name = outNodeName + (outNodeName[0] == '^');
        if (runtimeList) {
            if (runtimeList->lookupNode(blockPath, name)) {
                return true;
            }
        }

        // First-person hook (Player only)
        if (form->IsPlayer() && playerFirstPersonModel.lookupNode(blockPath, name) == nullptr) {
            return true;
        }
        return false;
    }

    bool hasRuntimeNodeModel(TESForm* form, char* formattedNode) {

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* modelPath = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, modelPath, outSuffix);

        NiBlockPathBase blockPath(outPath);
        ModelPath model(modelPath, outSuffix);

        if (runtimeList) {
            if (runtimeList->lookupModel(blockPath, model)) {
                return true;
            }
        }

        if (form->IsPlayer() && playerFirstPersonModel.lookupModel(blockPath, model) == nullptr) {
            return true;
        }
        return false;
    }

    bool hasRuntimeNodeRefModel(TESForm* form, TESForm* toAttach, char* formattedNode) {

        if (!form || !toAttach) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outSuffix = nullptr;
        const char* nop = nullptr; //NotUsed
        pUtils::formatString(formattedNode, outPath, nop, outSuffix);

        RefModel model(toAttach->refID, outSuffix);
        NiBlockPathBase blockPath(outPath);

        if (runtimeList) {
            if (runtimeList->lookupRefModel(blockPath, model)) {
                return true;
            }
        }

        if (form->IsPlayer() && playerFirstPersonModel.lookupRefModel(blockPath, model) == nullptr) {
            return true;
        }
        return false;
    }

    /// Register a new node‐path for `form`.
    /// If this is the first path for the form, set its JIP flag on.
    bool RegisterNode(UInt32 modIndex, TESForm* form, char* formattedNode, NiNode* root, bool insertIntoMap) {

        bool successful = false;

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, outNodeName, outSuffix);

        NiBlockPathBase blockPath(outPath);

        // skip a leading '^' if present
        const char* name = outNodeName + (outNodeName[0] == '^');
        if (runtimeList) {
            if (runtimeList->lookupNode(blockPath, name)) {
                return false;  // Already attached
            }
        }

        // First-person hook (Player only)
        if (form->IsPlayer() && playerFirstPersonModel.lookupNode(blockPath, name) == nullptr) {

            successful = playerFirstPersonModel.attachNode(modIndex, g_thePlayer->node1stPerson, blockPath, outNodeName, insertIntoMap);
            form->SetJIPFlag(kAttachHookFlags_InsertNode, true);

            //if (g_thePlayer->node1stPerson == root) {
                //Console_Print("root node was 1st person node");
            //}

        }

        if (!root && !insertIntoMap) {
            return false; //Model failed to load
        }

        if (!runtimeList) {
            // first-ever node for this form
            NiRuntimeNodeVector formRuntimeNodes;
            if (!formRuntimeNodes.attachNode(modIndex, root, blockPath, outNodeName, insertIntoMap)) {
                return successful;
            }
            auto result = nodeTree.try_emplace(form, std::move(formRuntimeNodes));

        }
        else if (!runtimeList->attachNode(modIndex, root, blockPath, outNodeName, insertIntoMap)) {
            return successful;
        }

        form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
        return true;

    }

    bool RegisterModel(UInt32 modIndex, TESForm* form, char* formattedNode, NiNode* root, bool insertIntoMap) {

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* modelPath = nullptr;
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, modelPath, outSuffix);

        if (!modelPath || !modelPathExists(modelPath)) {
            return false;
        }

        NiBlockPathBase blockPath(outPath);
        ModelPath model(modelPath, outSuffix);

        // Already attached?  Nothing to do.
        if (runtimeList && runtimeList->lookupModel(blockPath, model)) {
            return false;
        }

        // First-person (Player only)
        if (form->IsPlayer() && playerFirstPersonModel.lookupModel(blockPath, model) == nullptr) {

            playerFirstPersonModel.attachModel(modIndex, g_thePlayer->node1stPerson, blockPath, model, insertIntoMap);
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);

        }

        if (!root && !insertIntoMap) {
            return false; //Model failed to load
        }

        if (!runtimeList) {

            NiRuntimeNodeVector formRuntimeNodes;
            if (!formRuntimeNodes.attachModel(modIndex, root, blockPath, model, insertIntoMap)) {
                return false;
            }
            auto result = nodeTree.try_emplace(form, std::move(formRuntimeNodes));

        }
        else {

            if (!runtimeList->attachModel(modIndex, root, blockPath, model, insertIntoMap)) {
                return false;
            }

        }

        form->SetJIPFlag(kAttachHookFlags_AttachModel, true);
        return true;

    }

    bool RegisterRefModel(UInt32 modIndex, TESForm* form, TESForm* toAttach, char* formattedNode, NiNode* root, bool insertIntoMap) {

        if (!form) return false;
        if (!toAttach) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outSuffix = nullptr;
        const char* nop = nullptr; //NotUsed
        pUtils::formatString(formattedNode, outPath, nop, outSuffix);
        if (!outPath) {
            outPath = nop;
        }

        RefModel model(toAttach->refID, outSuffix);
        NiBlockPathBase blockPath(outPath);

        // Already attached?  Nothing to do.
        if (runtimeList && runtimeList->lookupRefModel(blockPath, model)) {
            return false;
        }

        ModelTemp formModel = model.getRuntimeModel(toAttach);
        if (!formModel.isValid()) {
            return false;
        }

        // First-person (Player only)
        if (form->IsPlayer() && playerFirstPersonModel.lookupRefModel(blockPath, model) == nullptr) {

            playerFirstPersonModel.attachRefModel(modIndex, g_thePlayer->node1stPerson, blockPath, model, formModel, insertIntoMap);
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);

        }

        if (!root && !insertIntoMap) {
            return false; //Model failed to load
        }

        if (!runtimeList) {

            NiRuntimeNodeVector formRuntimeNodes;
            if (!formRuntimeNodes.attachRefModel(modIndex, root, blockPath, model, formModel, insertIntoMap)) {
                return false;
            }
            auto result = nodeTree.try_emplace(form, std::move(formRuntimeNodes));

        }
        else {

            if (!runtimeList->attachRefModel(modIndex, root, blockPath, model, formModel, insertIntoMap)) {
                return false;
            }

        }

        form->SetJIPFlag(kAttachHookFlags_AttachModel, true);
        return true;

    }

    /// Remove one matching node‐path for `form`.
    /// If that was the last entry, clear its JIP flag.
    bool RemoveNode(TESForm* form, char* formattedNode, NiNode* root) {

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        if (!runtimeList) return false;

        const char* outPath = nullptr; //If they put a pipe
        const char* nodeName = nullptr; //Node name or model path
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, outPath, nodeName, outSuffix);

        NiBlockPathBase lookupPath(outPath);
        NiFixedString lookupName(nodeName);

        //Remove from first person
        if (form->IsPlayer()) {

            auto allNodesToRemove = playerFirstPersonModel.removeNode(lookupPath, lookupName);
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                g_thePlayer->node1stPerson->removeRuntimeNode(runtimeNode);
            }
            g_thePlayer->node1stPerson->UpdateTransformAndBounds(kNiUpdateData);

        }

        auto allNodesToRemove = runtimeList->removeNode(lookupPath, lookupName);
        if (root) {
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                root->removeRuntimeNode(runtimeNode);
            }
            root->UpdateTransformAndBounds(kNiUpdateData);
        }

        if (runtimeList->empty())
            form->SetJIPFlag(kAttachHookFlags_InsertNode, false);

        return true;
    }

    bool RemoveModel(TESForm* form, char* formattedNode, NiNode* root) {

        if (!form) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        if (!runtimeList) return false;

        const char* attachToNode = nullptr;
        const char* modelPath = nullptr; //Node name or model path
        const char* outSuffix = nullptr;
        pUtils::formatString(formattedNode, attachToNode, modelPath, outSuffix);

        if (!modelPath || !modelPathExists(modelPath)) {
            return false;
        }

        NiBlockPathBase lookupPath(attachToNode);
        ModelPath lookupModel(modelPath, outSuffix);

        //Remove from first person
        if (form->IsPlayer()) {

            auto allNodesToRemove = playerFirstPersonModel.removeModel(lookupPath, lookupModel);
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                g_thePlayer->node1stPerson->removeRuntimeNode(runtimeNode);
            }
            g_thePlayer->node1stPerson->UpdateTransformAndBounds(kNiUpdateData);
        }

        auto allNodesToRemove = runtimeList->removeModel(lookupPath, lookupModel);
        if (root) {
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                root->removeRuntimeNode(runtimeNode);
            }
            root->UpdateTransformAndBounds(kNiUpdateData);
        }

        if (runtimeList->empty())
            form->SetJIPFlag(kAttachHookFlags_AttachModel, false);

        return true;
    }

    bool RemoveRefModel(TESForm* form, TESForm* toRemove, char* formattedNode, NiNode* root) {

        if (!form) return false;
        if (!toRemove) return false;
        NiRuntimeNodeVector* runtimeList = getNodesList(form);

        if (!runtimeList) return false;

        const char* outPath = nullptr;
        const char* outSuffix = nullptr;
        const char* nop = nullptr; //NotUsed
        pUtils::formatString(formattedNode, outPath, nop, outSuffix);
        if (!outPath) {
            outPath = nop;
        }

        RefModel lookupModel(toRemove->refID, outSuffix);
        NiBlockPathBase lookupPath(outPath);

        //Remove from first person
        if (form->IsPlayer()) {

            auto allNodesToRemove = playerFirstPersonModel.removeRefModel(lookupPath, lookupModel);
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                g_thePlayer->node1stPerson->removeRuntimeNode(runtimeNode);
            }
            g_thePlayer->node1stPerson->UpdateTransformAndBounds(kNiUpdateData);
        }

        auto allNodesToRemove = runtimeList->removeRefModel(lookupPath, lookupModel);
        if (root) {
            for (auto& runtimeNode : std::views::reverse(allNodesToRemove)) {
                root->removeRuntimeNode(runtimeNode);
            }
            root->UpdateTransformAndBounds(kNiUpdateData);
        }

        if (runtimeList->empty())
            form->SetJIPFlag(kAttachHookFlags_AttachModel, false);

        return true;
    }

    NiRuntimeNodeVector* getNodesList(TESForm* form) {
        const auto iter = nodeTree.find(form);
        NiRuntimeNodeVector* pathList = nullptr;
        if (iter != nodeTree.end()) {
            pathList = &iter->second;
        }
        return pathList;
    };
    const NiRuntimeNodeVector* getNodesList(TESForm* form) const {
        const auto iter = nodeTree.find(form);
        const NiRuntimeNodeVector* pathList = nullptr;
        if (iter != nodeTree.end()) {
            pathList = &iter->second;
        }
        return pathList;
    };

};

