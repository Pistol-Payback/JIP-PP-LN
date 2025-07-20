#pragma once
#include "RuntimeNodeVector.hpp"
#include "NiBlockPath.hpp"
#include "ModelBuilder.hpp"
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

    static FormRuntimeModelManager& getSingleton() {
        static FormRuntimeModelManager s_nodeManager;
        return s_nodeManager;
    }

    /*
    [[nodiscard]] bool formatString(std::string_view input, std::string& outPath, std::string& outValue, std::string& outSuffix)
    {
        outPath.clear();
        outValue.clear();
        outSuffix.clear();

        if (input.empty())
            return false;

        const auto pipePos = input.find('|');
        std::string_view remaining;

        if (pipePos != std::string_view::npos) {
            outPath = input.substr(0, pipePos);      // may be empty
            remaining = input.substr(pipePos + 1);    // after ‘|’
            if (remaining.empty())
                return false;                         // “path|”
        }
        else {
            remaining = input;
        }

        if (!remaining.empty() && remaining.front() == '*') {
            const auto secondStar = remaining.find('*', 1);
            if (secondStar == std::string_view::npos || secondStar == 1)
                return false;                         // no closing ‘*’ or empty
            outSuffix = remaining.substr(1, secondStar - 1);
            remaining = remaining.substr(secondStar + 1); // after suffix
        }

        // ── 3. Remaining text must be the value ────────────────────
        if (remaining.empty())
            return false;

        outValue = remaining;
        return true;
    }
    */

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
        if (form->IsPlayer() && s_pc1stPersonNode) {
            if (NiAVObject* path = s_pc1stPersonNode->DeepSearchBySparsePath(pathToFind))
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
        return false;
        // Fallback: fully built model search
        if (const char* path = form->GetModelPath()) {
            ModelTemp temp = ModelTemp(path);
            ModelBuilder liveBuilder(temp.clonedNode, getNodesList(form));
            if (NiNode* liveRoot = liveBuilder.model) {
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
            if (s_pc1stPersonNode->DeepSearchBySparsePath(NiBlockPathBase(fullPath)))
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
        return false;
        // Fallback: fully built model search
        if (const char* path = form->GetModelPath()) {
            ModelTemp temp = ModelTemp(path);
            ModelBuilder liveBuilder(temp.clonedNode, getNodesList(form));
            if (NiNode* liveRoot = liveBuilder.model) {
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

        bool destroy = false;

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, outNodeName, outSuffix);

        if (pathList && pathList->findNode(outPath, outNodeName)) {
            return false; //Already attached
        }

        //Attach to first person
        if (form->IsPlayer()) {
            ModelBuilder fpBuilder(s_pc1stPersonNode, getNodesList(form));
            NiRuntimeNode fullPath = fpBuilder.attachNewNode(outPath, outNodeName);
            if (!fullPath.isValid()) {
                return false;
            }
            playerFirstPersonModel.addExact(std::move(fullPath));
            form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
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
            }

        }
        ModelBuilder model(root, getNodesList(form));
        NiRuntimeNode fullPath = model.attachNewNode(outPath, outNodeName);

        if (!fullPath.isValid()) {
            return false;
        }

        if (!pathList) {
            // first-ever node for this form
            nodeTree.try_emplace(form, std::move(fullPath));
            form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
            return true;
        }
        else {
            bool added = pathList->addExact(std::move(fullPath));
            if (added) {
                form->SetJIPFlag(kAttachHookFlags_InsertNode, true);
            }
            return added;
        }

    }

    bool RegisterModel(TESForm* form, const char* formattedNode, NiNode* root) {

        bool added = false;
        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        const char* outPath = nullptr;
        const char* outNodeName = nullptr;
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, outNodeName, outSuffix);

        // Already attached?  Nothing to do.
        if (pathList && pathList->findNode(outPath, outNodeName)) {
            return false;
        }

        // First-person hook (Player only)
        if (form->IsPlayer()) {
            ModelBuilder fpBuilder(s_pc1stPersonNode, getNodesList(form));

            NiRuntimeNode fullPath = fpBuilder.attachNewModel(outPath, outNodeName, outSuffix);

            if (!fullPath.isValid()) {
                return false;
            }
            playerFirstPersonModel.addExact(std::move(fullPath));
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);

            added = pathList->addExact(std::move(fullPath));

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
            }

        }

        ModelBuilder builder(root, getNodesList(form));
        NiRuntimeNode fullPath = builder.attachNewModel(outPath, outNodeName, outSuffix);

        if (!fullPath.isValid()) {
            return false;
        }

        if (!pathList) {
            nodeTree.try_emplace(form, std::move(fullPath));
            form->SetJIPFlag(kAttachHookFlags_AttachModel, true);
            return true;
        }
        else if (!added) {
            added = pathList->addExact(std::move(fullPath));
            if (added) {
                form->SetJIPFlag(kAttachHookFlags_AttachModel, true);
            }
            return added;
        }
    }

    /// Remove one matching node‐path for `form`.
    /// If that was the last entry, clear its JIP flag.
    bool RemoveNode(TESForm* form, const char* formattedNode, uint8_t jipFlag) {

        if (!form) return false;
        NiRuntimeNodeVector* pathList = getNodesList(form);

        if (!pathList) return false;

        const char* outPath = nullptr;
        const char* outValue = nullptr;
        const char* outSuffix = nullptr;
        formatString(formattedNode, outPath, outValue, outSuffix);

        //Remove from first person
        if (form->IsPlayer()) {
            if (!playerFirstPersonModel.remove(outPath, outValue))
                return false;   //Didn't find node
        }

        if (!pathList->remove(outPath, outValue))
            return false;   //Didn't find node

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

