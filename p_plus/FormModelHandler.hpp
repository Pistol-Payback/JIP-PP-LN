#pragma once
#include "RuntimeNodeVector.hpp"
#include "NiBlockPath.hpp"
#include "NiBlockPathBuilder.hpp"
#include <unordered_map>

enum AttachHookFlags : uint8_t
{
    kAttachHookFlags_InsertNode = 64,
    kAttachHookFlags_AttachModel = 128,
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

public:

    NiRuntimeNodeVector& getFirstPersonModelList() {
        return playerFirstPersonModel;
    }

    static FormRuntimeModelManager& getSingleton() {
        static FormRuntimeModelManager s_nodeManager;
        return s_nodeManager;
    }

    static bool formatString(const char* toFormat, const char*& outPath, const char*& outValue, const char*& outSuffix)
    {

        outPath = nullptr;
        outValue = nullptr;
        outSuffix = nullptr;

        if (!toFormat || !*toFormat)
            return false;

        // Try to find the pipe first
        char* pipe = FindChr(toFormat, '|');
        char* rem = nullptr;

        if (pipe) {
            // Split at pipe
            *pipe = '\0';
            outPath = toFormat;
            rem = pipe + 1;
            if (!*rem)
                return false;  // nothing after pipe
        }
        else {
            // No pipe: whole string is in rem
            rem = const_cast<char*>(toFormat);
        }

        // Now look for optional *suffix* at start of rem
        if (rem[0] == '*') {
            char* second = FindChr(rem + 1, '*');
            if (second && second > rem + 1) {
                *second = '\0';
                outSuffix = rem + 1;
                rem = second + 1;
            }
        }

        if (!*rem)
            return false;  // no actual value

        outValue = rem;
        return true;
    }


    static inline bool splitPathHead(const char* fullPath, char* buffer,  size_t bufSize, const char*& outPrefix, const char*& outLeaf) noexcept {

        size_t len = strnlen(fullPath, bufSize);
        if (len == bufSize) return false;              // too long

        // copy + null
        std::memcpy(buffer, fullPath, len + 1);

        // find last backslash
        char* slash = std::strrchr(buffer, '\\');
        if (slash) {
            *slash = '\0';                             // terminate prefix
            outPrefix = buffer;
            outLeaf = slash + 1;
        }
        else {
            outPrefix = "";                             // empty prefix
            outLeaf = buffer;                         // whole thing is the leaf
        }
        return true;
    }

    // Returns true if any registered NiNodePath for this form matches `toFind`
    // Takes a baseform
    bool HasNode(TESForm* form, const NiBlockPathBase& pathToFind) const noexcept {

        if (!form)
            return false;

        // Check player first-person if needed
        if (form->IsPlayer() && g_thePlayer->node1stPerson) {
            if (g_thePlayer->node1stPerson->DeepSearchBySparsePath(pathToFind))
               return true;
        }

        if (!form->IsBoundObject()) {
            return false;
        }

        // Check our registered cache using buf (prefix) + leafName
        const NiRuntimeNodeVector* pathList = getNodesList(form);
        if (pathList) {
            if (pathList->findNode(pathToFind))
                return true;
        }

        // Fallback: fully built model search
        if (const char* path = form->GetModelPath()) {
            ModelTemp temp = ModelTemp(path);
            temp.clonedNode->attachRuntimeNodes(getNodesList(form));
            if (NiNode* liveRoot = temp.clonedNode) {
                if (liveRoot->DeepSearchBySparsePath(pathToFind)) {
                    return true;
                }

            }
        }
    }

    // Takes a baseform
    bool HasNode(TESForm* form, const char* fullPath) const noexcept {

        if (!form || !fullPath || !*fullPath)
            return false;

        // Check player first-person if needed
        if (form->IsPlayer()) {
            if (g_thePlayer->node1stPerson->DeepSearchBySparsePath(NiBlockPathBase(fullPath)))
                return true;
        }

        if (!form->IsBoundObject()) {
            return false;
        }

        constexpr size_t BUF_SIZE = 260;
        char buf[BUF_SIZE];
        const char* prefix, *nodeHead;
        if (!splitPathHead(fullPath, buf, BUF_SIZE, prefix, nodeHead))
            return false;

        // Check our registered cache using buf (prefix) + nodeName
        const NiRuntimeNodeVector* pathList = getNodesList(form);
        if (pathList) {
            if (pathList->findNode(prefix, nodeHead))
                return true;
        }

        // Fallback: fully built model search
        if (const char* path = form->GetModelPath()) {
            ModelTemp temp = ModelTemp(path);
            temp.clonedNode->attachRuntimeNodes(getNodesList(form));
            if (NiNode* liveRoot = temp.clonedNode) {
                if (liveRoot->DeepSearchBySparsePath(NiBlockPathBase(fullPath))) {
                    return true;
                }
            }
        }

        return false;
    }

    /// Register a new node‐path for `form`.
    /// If this is the first path for the form, set its JIP flag on.
    bool RegisterNode(TESForm* form, const char* formattedNode, NiNode* root) {

        bool successful = false;

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, outNodeName, outSuffix);

        if (pathList) {
            // skip a leading '^' if present
            const char* name = outNodeName + (outNodeName[0] == '^');
            if (pathList->findNode(outPath, name)) {
                return false;  // Already attached
            }
        }

        NiBlockPathBase blockPath(outPath);
        // First-person hook (Player only)
        if (form->IsPlayer()) {

            successful = playerFirstPersonModel.attachNode(g_thePlayer->node1stPerson, blockPath, outNodeName);
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);

            if (g_thePlayer->node1stPerson == root) {
                Console_Print("root node was 1st person node");
            }

        }

        //Build model
        ModelTemp temp;
        if (!root) {
            const char* _modelPath = form->GetModelPath();
            if (!_modelPath) {
                return successful;
            }

            if (temp.load(_modelPath)) { //Load a temp that will get freed at the end of this scope
                root = temp.clonedNode;
                root->attachRuntimeNodes(getNodesList(form));
            }

        }

        if (!pathList) {
            // first-ever node for this form
            NiRuntimeNodeVector formRuntimeNodes;
            if (!formRuntimeNodes.attachNode(root, blockPath, outNodeName)) {
                return successful;
            }
            auto result = nodeTree.try_emplace(form, std::move(formRuntimeNodes));
            form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
        }
        else {

            if (!pathList->attachNode(root, blockPath, outNodeName)) {
                return successful;
            }
            form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
        }

        return true;

    }

    bool RegisterModel(TESForm* form, const char* formattedNode, NiNode* root) {

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        const char* outPath = nullptr;
        const char* modelPath = nullptr;
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, modelPath, outSuffix);

        if (!modelPath || !modelPathExists(modelPath)) {
            return false;
        }

        // Already attached?  Nothing to do.
        if (pathList && pathList->findNode(outPath, modelPath)) {
            return false;
        }

        NiBlockPathBase blockPath(outPath);

        // First-person (Player only)
        if (form->IsPlayer()) {

            playerFirstPersonModel.addModel(g_thePlayer->node1stPerson, blockPath, modelPath, outSuffix);
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);

        }

        //Build model
        ModelTemp temp;
        if (!root) {

            const char* _modelPath = form->GetModelPath();
            if (!_modelPath) {
                return false;
            }

            if (temp.load(_modelPath)) { //Load a temp that will get freed at the end of this scope
                root = temp.clonedNode;
                root->attachRuntimeNodes(getNodesList(form));
            }

        }

        if (!pathList) {

            NiRuntimeNodeVector formRuntimeNodes;
            if (!formRuntimeNodes.addModel(root, blockPath, modelPath, outSuffix)) {
                return false;
            }
            auto result = nodeTree.try_emplace(form, std::move(formRuntimeNodes));

        }
        else {

            if (!pathList->addModel(root, blockPath, modelPath, outSuffix)) {
                return false;
            }

        }

        form->SetJIPFlag(kAttachHookFlags_AttachModel, true);
        return true;

    }

    /// Remove one matching node‐path for `form`.
    /// If that was the last entry, clear its JIP flag.
    bool RemoveNode(TESForm* form, const char* formattedNode, uint8_t jipFlag, NiNode* root) {

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        if (!pathList) return false;

        const char* outPath = nullptr; //If they put a pipe
        const char* nodeName = nullptr; //Node name or model path
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, nodeName, outSuffix);

        NiBlockPathBase blockPath = NiBlockPathBase(outPath, nodeName);

        //Remove from first person
        if (form->IsPlayer()) {

            std::vector<NiRuntimeNode> toRemoveList = playerFirstPersonModel.moveAllContainingPaths(blockPath);
            if (toRemoveList.empty()) {
                return false;   //Didn't find a node
            }

            for (auto& node : toRemoveList) {
                g_thePlayer->node1stPerson->removeRuntimeNode(node);
            }

        }

        std::vector<NiRuntimeNode> toRemoveList = pathList->moveAllContainingPaths(blockPath);
        if (toRemoveList.empty()) {
            return false;   //Didn't find a node
        }

        if (root) {
            for (auto& node : toRemoveList) {
                root->removeRuntimeNode(node);
            }
        }

        if (pathList->empty())
            form->SetJIPFlag(jipFlag, false);

        return true;
    }

    bool RemoveModel(TESForm* form, const char* formattedNode, uint8_t jipFlag, NiNode* root) {

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        if (!pathList) return false;

        const char* attachToNode = nullptr;
        const char* modelPath = nullptr; //Node name or model path
        const char* outSuffix = nullptr;
        formatString(formattedNode, attachToNode, modelPath, outSuffix);

        if (!modelPath || !modelPathExists(modelPath)) {
            return false;
        }

        //Rework this, we only need to extract the root node, no need to copy the whole tree
        ModelTemp tempModel = ModelTemp(modelPath);

        NiFixedString fullBlockName;
        if (outSuffix) {
            fullBlockName = NiFixedString((tempModel.clonedNode->m_blockName.ToString() + std::string(outSuffix)).c_str());
        }
        else {
            fullBlockName = tempModel.clonedNode->m_blockName;
        }


        NiBlockPathBase blockPath = NiBlockPathBase(attachToNode, fullBlockName);

        //Remove from first person
        if (form->IsPlayer()) {

            std::vector<NiRuntimeNode> toRemoveList = playerFirstPersonModel.moveAllContainingPaths(blockPath);
            if (toRemoveList.empty()) {
                return false;   //Didn't find a node
            }

           for (auto& node : toRemoveList) {
               g_thePlayer->node1stPerson->removeRuntimeNode(node);
           }

        }

        std::vector<NiRuntimeNode> toRemoveList = pathList->moveAllContainingPaths(blockPath);
        if (toRemoveList.empty()) {
            return false;   //Didn't find a node
        }

        if (root) {
            for (auto& node : toRemoveList) {
                root->removeRuntimeNode(node);
            }
        }

        if (pathList->empty())
            form->SetJIPFlag(jipFlag, false);

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

