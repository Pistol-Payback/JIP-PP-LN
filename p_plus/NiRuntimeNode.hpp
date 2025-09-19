#pragma once
#include "NiBlockPathBuilder.hpp"

struct NiRuntimeNode {

    NiRuntimeNode()
        : sparsePath{}     // empty buffer, nodes==nullptr, length==0
        , cachedPath{}     // empty buffer, nodes==nullptr, length==0
        , node{}     // empty string
        , value{}    // type==kInvalid
    {}

    NiRuntimeNode(const NiRuntimeNode& other) : 
        node(other.node), 
        value(other.value), 
        cachedPath(other.cachedPath.createCopy()), 
        sparsePath(other.sparsePath.createCopy()) 
    {};

    NiRuntimeNode(NiRuntimeNode&&) noexcept = default;
    NiRuntimeNode& operator=(NiRuntimeNode&&) noexcept = default;
    NiRuntimeNode& operator=(const NiRuntimeNode&) = default;
    ~NiRuntimeNode() = default;

    NiRuntimeNode(UInt32 modIndex, NiBlockPathBase&& path, const NiFixedString& leafName, const NiToken value, NiBlockPathStatic&& fullPath)
        : 
        modIndex(modIndex),
        value(value),
        sparsePath(std::move(path)),
        node(leafName),
        cachedPath(std::move(fullPath))
    {}

    NiRuntimeNode(UInt32 modIndex, NiBlockPathBase&& path, const NiFixedString& leafName, const NiToken value = {})
        :
        modIndex(modIndex),
        value(value),
        sparsePath(std::move(path)),
        node(leafName)
    {}

    NiRuntimeNode(UInt32 modIndex, const char* fullPath, const char* leafName, const NiToken value = {}) : modIndex(modIndex), value(value), node(leafName), cachedPath(fullPath) {}

    NiToken  value;

    // Node, or model root node
    NiFixedString       node;
    NiBlockPathBase     sparsePath;

    NiBlockPathStatic   cachedPath;  // the sequence of parent‐names

    UInt32 modIndex;

    inline std::string toString(bool leadingSlash = false, bool includeLeaf = true) const
    {
        // fast path: build once from sizes
        std::string out = sparsePath.toString(leadingSlash);
        if (includeLeaf && node.isValid()) {
            if (!out.empty() || leadingSlash) out.push_back('\\');
            out.append(node.CStr());
        }
        return out;
    }

    inline std::string toDebugString(bool leadingSlash = false, bool showTypeInfo = true, bool showCachedPath = false) const
    {

        std::string result = toString(leadingSlash, true);
        if (showTypeInfo) {
            if (value.isModel()) {
                const auto& m = value.getModelPath();
                result.append("  [Model: ");
                if (m.suffix.isValid()) { result.push_back('*'); result.append(m.suffix.CStr()); result.push_back('*'); result.push_back(' '); }
                result.append(m.path.isValid() ? m.path.CStr() : "<null>");
                result.push_back(']');
            }
            else if (value.isRefID()) {
                const auto& r = value.getRefModel();
                char idbuf[11]; // "0xFFFFFFFF"
                std::snprintf(idbuf, sizeof(idbuf), "0x%08X", r.refID);
                result.append("  [Ref: ").append(idbuf);
                if (r.suffix.isValid()) { result.append(" *").append(r.suffix.CStr()).append("*"); }
                result.push_back(']');
            }
            else if (value.isLink()) {
                const auto& l = value.getLink();
                result.append("  [^parent of ").append(l.childObj.isValid() ? l.childObj.CStr() : "<null>").append("]");
            }
            else {
                result.append("  [Node]");
            }
        }

        if (showCachedPath && cachedPath.size() > 0) {
            result.append("  | cached=").append(cachedPath.toString(leadingSlash));
        }
        return result;
    }

    inline void reverseToFormatTo(pSmallBufferWriter& buf, TESForm*& outRef, NiToken::Type& outType) const noexcept
    {
        buf.reset();
        outRef = nullptr;

        sparsePath.appendSparsePath(buf);
        if (buf.empty()) buf.pushChar('|');

        if (value.isModel()) {
            outType = NiToken::Type::Model;
            value.getModelPath().reverseFormatTo(buf);                // "*Suffix*ModelPath" or "ModelPath"
        }
        else if (value.isRefID() && value.getRefModel().suffix.isValid()) {
            outType = NiToken::Type::RefID;
            value.getRefModel().reverseFormatTo(buf, outRef);         // "*Suffix*"
        }
        else if (value.isLink()) {
            outType = NiToken::Type::Link;
            buf.pushChar('^');
            buf.appendFixed(node);
        }
        else {
            outType = NiToken::Type::Link;
            buf.appendFixed(node);
        }
    }

    //checks a sparse path, path is less exact, but quicker. 
    bool containsSparsePath(const NiBlockPathView& blockPath, const NiFixedString& nodeName) const noexcept {
        return node == nodeName && (cachedPath.empty() ? sparsePath.containsSparsePath(blockPath) : cachedPath.containsSparsePath(blockPath));
    }

    //If we want to check part of the path suffix, rather than the entier path.
    bool hasPathSuffixToNode(const NiBlockPathView& blockPath, const NiFixedString& nodeName) const noexcept {
        return node == nodeName && cachedPath.containsPathSuffix(blockPath);
    }

    //Is it faster to strcmp, or register a new fixedString?
    bool hasPathSuffixToNode(const char* pathToMatch, const char* nodeName) const noexcept {
        return strcmp(node.CStr(), nodeName) == 0 && cachedPath.containsPathSuffix(pathToMatch);
    }

    // Full‐equality: both node AND entire path must match exactly
    bool operator==(NiRuntimeNode const& rhs) const noexcept {
        return node == rhs.node && cachedPath == rhs.cachedPath;
    }

    bool contains(const char* pathToMatch, NiFixedString& nodeName) const noexcept {
        return node == nodeName || cachedPath.contains(pathToMatch);
    }

    bool contains(NiRuntimeNode const& other) const noexcept {
        return node == other.node || cachedPath.contains(other.cachedPath);
    }

    inline bool isValid() const {
        return cachedPath.size() && sparsePath.size() && node.isValid();
    }

};