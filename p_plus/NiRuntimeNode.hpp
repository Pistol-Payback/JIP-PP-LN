#pragma once
#include "NiBlockPathBuilder.hpp"

struct NiRuntimeNode {

    NiRuntimeNode()
        : path{}     // empty buffer, nodes==nullptr, length==0
        , node{}     // empty string
        , value{}    // type==kInvalid
    {}

    NiRuntimeNode(const NiRuntimeNode&) = default;
    NiRuntimeNode(NiRuntimeNode&&) noexcept = default;
    NiRuntimeNode& operator=(NiRuntimeNode&&) noexcept = default;
    NiRuntimeNode& operator=(const NiRuntimeNode&) = default;
    ~NiRuntimeNode() = default;

    NiRuntimeNode(NiBlockPathStatic&& paths, const NiFixedString& leafName, const NiToken value = {})
        : 
        value(value),
        node(leafName),
        path(std::move(paths))
    {}

    NiRuntimeNode(const char* fullPath, const char* leafName, const NiToken value = {}) : value(value), node(leafName), path(fullPath) {}

    NiToken  value;

    // Node, or model root node
    NiFixedString       node;
    NiBlockPathStatic   path;  // the sequence of parent‐names
    NiRefPtr<NiNode>    runtimeNode; //Can be null

    inline bool isValid() const { return node.isValid(); }

    //checks a sparse path, path is less exact, but quicker. 
    bool isNode(const NiBlockPathView& blockPath, const NiFixedString& nodeName) const noexcept {
        return node == nodeName && path.containsSparsePath(blockPath);
    }

    //checks a sparse path, path is less exact, but quicker. 
    bool isNode(const char* pathToMatch, const char* nodeName) const noexcept {
        return strcmp(node.CStr(), nodeName) == 0 && path.containsSparsePath(pathToMatch);
    }

    //If we want to check part of the path suffix, rather than the entier path.
    bool hasPathSuffixToNode(const NiBlockPathView& blockPath, const NiFixedString& nodeName) const noexcept {
        return node == nodeName && path.containsPathSuffix(blockPath);
    }

    //Is it faster to strcmp, or register a new fixedString?
    bool hasPathSuffixToNode(const char* pathToMatch, const char* nodeName) const noexcept {
        return strcmp(node.CStr(), nodeName) == 0 && path.containsPathSuffix(pathToMatch);
    }

    // Full‐equality: both node AND entire path must match exactly
    bool operator==(NiRuntimeNode const& rhs) const noexcept {
        return node == rhs.node && path == rhs.path;
    }

    bool contains(const char* pathToMatch, const char* nodeName) const noexcept {
        return strcmp(node.CStr(), nodeName) == 0 || path.contains(pathToMatch);
    }

    bool contains(const char* pathToMatch, NiFixedString& nodeName) const noexcept {
        return node == nodeName || path.contains(pathToMatch);
    }

    bool contains(NiRuntimeNode const& other) const noexcept {
        return node == other.node || path.contains(other.path);
    }

};