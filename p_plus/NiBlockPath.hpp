#pragma once
#include <array>
#include <cassert>
#include <numeric>
#include <variant>
#include <stdexcept>
#include <cstdint>
#include "internal/netimmerse.h"
#include "internal/jip_core.h"
#include "p_plus/NiFixedString.hpp"

#include <concepts>

template<typename U>
concept PathPart = std::same_as<U, NiFixedString> || std::same_as<U, NiBlockPathBase>;

struct IndexLinkedList {

    UInt16 previous;
    UInt16 next;

};

struct RefModel {
    NiFixedString suffix;
    uint32_t    refID;

    RefModel() noexcept
        : suffix(nullptr)
        , refID(0)
    {}

    RefModel(uint32_t id, const char* sfx = nullptr) noexcept
        : suffix(sfx)
        , refID(id)
    {}
};

struct ModelPath {
    NiFixedString suffix;
    NiFixedString path;

    ModelPath() noexcept
        : suffix(nullptr)
        , path(nullptr)
    {}

    ModelPath(const char* pth, const char* sfx) noexcept
        : suffix(sfx)
        , path(pth)
    {}
};

struct NiToken {
    //–– the types we can hold ––
    using InvalidList = std::monostate;
    using TokenVariant = std::variant<InvalidList, RefModel, ModelPath, IndexLinkedList>;

    enum class Type : uint8_t {
        Invalid = 0,
        RefID = 1,
        Model = 2,
        Link = 3,
    };

    //–– storage ––
    TokenVariant data;

    //–– ctors ––
    NiToken() noexcept : data(InvalidList{}) {}
    explicit NiToken(uint32_t id, const char* suffix = nullptr)
        : data(RefModel{ id, suffix }) {}
    explicit NiToken(const char* path, const char* suffix)
        : data(ModelPath{ path, suffix }) {}
    explicit NiToken(const IndexLinkedList& l) : data(l) {}

    //–– copy/move/assign/destruct –– defaults are correct for std::variant
    NiToken(const NiToken&) = default;
    NiToken(NiToken&&) noexcept = default;
    NiToken& operator=(const NiToken&) = default;
    NiToken& operator=(NiToken&&) noexcept = default;
    ~NiToken() = default;

    //–– introspection ––
    Type type() const noexcept {
        return static_cast<Type>(data.index());
    }
    bool isInvalid() const noexcept { return std::holds_alternative<InvalidList>(data); }
    bool isRefID()   const noexcept { return std::holds_alternative<RefModel>(data); }
    bool isModel()   const noexcept { return std::holds_alternative<ModelPath>(data); }
    bool isLink()    const noexcept { return std::holds_alternative<IndexLinkedList>(data); }

    //–– accessors with runtime checks ––
    const RefModel& getRefModel() const {
        if (auto p = std::get_if<RefModel>(&data)) return *p;
        throw std::logic_error("NiToken::getRefModel() on non-refID token");
    }

    const ModelPath& getModelPath() const {
        if (auto p = std::get_if<ModelPath>(&data)) return *p;
        throw std::logic_error("NiToken::getModelPath() on non-model token");
    }

    const IndexLinkedList& getLink() const {
        if (auto p = std::get_if<IndexLinkedList>(&data)) return *p;
        throw std::logic_error("NiToken::getLink() on non-link token");
    }

};

struct NiBlockPathView {

protected:
    NiFixedString* _nodes;    // pointer to the first segment
    uint32_t       _length;   // number of segments

public:
    // default: empty view
    constexpr NiBlockPathView() noexcept
        : _nodes(nullptr), _length(0)
    {}

    // construct from raw pointer+length
    constexpr NiBlockPathView(NiFixedString* nodes, uint32_t length) noexcept
        : _nodes(nodes), _length(length)
    {}

    // iteration
    constexpr const NiFixedString* begin()  const noexcept { return _nodes; }
    constexpr const NiFixedString* end()    const noexcept { return _nodes + _length; }

    const NiFixedString& back() const {
        if (empty()) {
            throw std::out_of_range(
                "NiBlockPathView::back(): called on an empty path view"
            );
        }
        return _nodes[_length - 1];
    }

    constexpr uint32_t             size()   const noexcept { return _length; }
    constexpr bool                 empty()  const noexcept { return _length == 0; }

    // random access
    constexpr const NiFixedString& operator[](uint32_t i) const noexcept {
        return _nodes[i];
    }

    // equality
    bool operator==(NiBlockPathView rhs) const noexcept {
        if (_length != rhs._length) return false;
        return std::memcmp(_nodes, rhs._nodes, _length * sizeof(NiFixedString)) == 0;
    }

    bool operator!=(NiBlockPathView rhs) const noexcept {
        return !(*this == rhs);
    }

    friend bool operator<(const NiBlockPathView& lhs, const NiBlockPathView& rhs) noexcept {
        return lhs._length < rhs._length;
    }
    friend bool operator>(const NiBlockPathView& lhs, const NiBlockPathView& rhs) noexcept {
        return lhs._length > rhs._length;
    }

    friend NiBlockPathBase;
    friend NiBlockPath;

};


struct NiBlockPathBase : public NiBlockPathView {

public:
    // default: empty
    NiBlockPathBase() noexcept
        : NiBlockPathView(nullptr, 0)
    {}

    const NiBlockPathView makeView(uint32_t newSize) const {
        return NiBlockPathView(_nodes, newSize);
    }

    NiBlockPathBase(NiBlockPathBase&& other) noexcept
        : NiBlockPathView(other._nodes, other._length)
    {
        other._nodes = nullptr;
        other._length = 0;
    }

    NiBlockPathBase& operator=(NiBlockPathBase&& other) noexcept {

        if (this != &other) {

            // destroy our own contents
            for (uint32_t i = 0; i < _length; ++i)
                _nodes[i].~NiFixedString();
            ::operator delete[](_nodes);

            // then steal
            _nodes = other._nodes;
            _length = other._length;
            other._nodes = nullptr;
            other._length = 0;
        }
        return *this;

    }

    template<PathPart... Parts>
    NiBlockPathBase(const Parts&... parts) {

        static_assert((!std::is_pointer_v<Parts> && ...),"NiBlockPathBase: constructor parts must not be pointers");

        // Compute total length
        _length = (partLength(parts) + ...);

        // Allocate exactly the right buffer
        if (_length > 0) {
            _nodes = static_cast<NiFixedString*>(::operator new[](_length * sizeof(NiFixedString)));
        }
        else {
            _nodes = nullptr;
            return;
        }

        // Construct in-place from each part
        uint32_t off = 0;
        (void)std::initializer_list<int>{
            (parts
                ? ( // if non‐empty, construct it in place
                    new (&_nodes[off++]) NiFixedString(parts),
                    0         // the comma‐operator yields an int for the init_list
                    )
                : 0            // if empty, do nothing
                )...
        };
    }

    template<std::input_iterator It> requires std::convertible_to<std::iter_value_t<It>,NiFixedString>
        NiBlockPathBase(It first, It last) {
        _length = static_cast<uint32_t>(std::distance(first, last));
        if (_length) {
            _nodes = static_cast<NiFixedString*>(::operator new[](_length * sizeof(NiFixedString)));
            uint32_t idx = 0;
            for (auto it = first; it != last; ++it, ++idx) {
                new (&_nodes[idx]) NiFixedString(*it);
            }
        }
        else {
            _nodes = nullptr;
        }
    }

    // C-string: split on ‘\’ into segments
    NiBlockPathBase(const char* pathString) {

        if (!pathString || !*pathString) {
            _nodes = nullptr;
            _length = 0;
            return;
        }

        // count segments
        uint32_t cnt = 1;
        for (auto p = pathString; *p; ++p)
            if (*p == '\\') ++cnt;
        _length = cnt;

        // allocate raw storage
        _nodes = static_cast<NiFixedString*>(::operator new[](_length * sizeof(NiFixedString)));

        // 3) split & construct each segment by length
        uint32_t off = 0;
        const char* start = pathString;
        for (const char* p = pathString; ; ++p) {
            if (*p == '\\' || *p == '\0') {

                size_t segLen = p - start;

                //We lie and modify the string, but restor it after so no one knows. Shhh
                char saved = *p;
                const_cast<char*>(p)[0] = '\0';
                new (&_nodes[off++]) NiFixedString(start);
                const_cast<char*>(p)[0] = saved;

                if (*p == '\0')
                    break;

                start = p + 1;
            }
        }

    }

    ~NiBlockPathBase() {
        for (uint32_t i = 0; i < _length; ++i) {
            _nodes[i].~NiFixedString();
        }
        ::operator delete[](_nodes);
    }

protected:

    // Only count 1 if seg is non-null
    static size_t partLength(const NiFixedString& seg) noexcept {
        return seg ? 1u : 0u;
    }
    // A subpath of length zero contributes nothing
    static size_t partLength(const NiBlockPathBase& bp) noexcept {
        return bp._length;
    }

};

struct NiBlockPath : public NiBlockPathBase {

    //Original Path
    std::string raw; //for fast comparision lookups, so we dont need to create a bunch of NiFixedStrings

    //–– ctors / dtor ––
    NiBlockPath() noexcept
        : NiBlockPathBase()
        , raw()
    {}

    // move‐ctor
    NiBlockPath(NiBlockPath&& other) noexcept
        : NiBlockPathBase(std::move(other))   // steals nodes & length
        , raw(std::move(other.raw))           // steals the string
    {}

    template<typename It>
    NiBlockPath(It begin, It end) : NiBlockPathBase(begin, end), raw()
    {
        if (_length) {
            // Preallocate enough space: sum of node lengths + (length-1) backslashes
            size_t reserveSize = (_length - 1) + std::accumulate(_nodes, _nodes + _length, size_t(0),
                    [](size_t acc, const NiFixedString& str) {
                        return acc + str.Length();
                    });
            raw.reserve(reserveSize);

            // Join with '\'
            for (uint32_t i = 0; i < _length; ++i) {
                if (i) raw.push_back('\\');
                raw.append(_nodes[i].CStr());
            }
        }
    }

    // from backslash-delimited C‐string "A\B\C"
    NiBlockPath(const char* pathString) : NiBlockPathBase(), raw(pathString ? pathString : "")
    {
        if (raw.empty())
            return;

        // count segments
        _length = static_cast<uint32_t>(std::count(raw.begin(), raw.end(), '\\')) + 1;
        _nodes = new NiFixedString[_length];

        // split and Set() each segment
        const char* start = raw.c_str();
        uint32_t idx = 0;
        for (size_t i = 0; i <= raw.size(); ++i) {
            if (i == raw.size() || raw[i] == '\\') {
                size_t segLen = (raw.c_str() + i) - start;
                std::string seg(start, segLen);
                _nodes[idx++] = seg.c_str();
                start = raw.c_str() + i + 1;
            }
        }
    }

    ~NiBlockPath() = default;

    // disallow copy
    NiBlockPath(const NiBlockPath&) = delete;
    NiBlockPath& operator=(const NiBlockPath&) = delete;

    //Only allow move
    NiBlockPath& operator=(NiBlockPath&& other) noexcept {
        if (this != &other) {
            NiBlockPathBase::operator=(std::move(other));
            raw = std::move(other.raw);
        }
        return *this;
    }

    // for searching existing blocks. we can check the ref count to make sure full path exists
    bool pathExists() const noexcept {
        for (auto& seg : *this) {
            if (seg.refCount() <= 1)
                return false;
        }
        return true;
    }

    NiFixedString& operator[](uint32_t i) { return _nodes[i]; }
    const NiFixedString& operator[](uint32_t i) const { return _nodes[i]; }

    //Suffix-match, checks if the end half of the array matches the entier query.
    bool containsPathSuffix(const NiBlockPathView& query) const noexcept {
        if (query._length > _length) return false;
        uint32_t off = _length - query._length;
        // pointer-compare is fine because equal strings are interned to identical pointers
        return std::memcmp(&_nodes[off], query._nodes, query._length * sizeof(NiFixedString)) == 0;
    }

    // suffix-match against a raw C-string, e.g. "\\B\\C"
    bool containsPathSuffix(const char* query) const noexcept {

        if (!query || !*query) return false;

        size_t len = std::strlen(query);
        if (len > raw.size()) return false;
        // compare tail of raw
        return raw.compare(raw.size() - len, len, query) == 0;

    }

    bool containsSparsePath(const char* query) const noexcept {

        // Empty query always matches
        if (!query || !*query) return true;

        std::string_view rv(raw);

        const char* segStart = query;
        size_t      pos = 0;

        while (*segStart) {

            const char* sep = std::strchr(segStart, '\\');
            size_t segLen = sep ? (sep - segStart) : std::strlen(segStart);
            std::string_view part(segStart, segLen);
            size_t found = rv.find(part, pos);
            if (found == std::string_view::npos)
                return false;

            pos = found + segLen;

            if (sep) { segStart = sep + 1; }
            else { break; }

        }

        return true;
    }

    bool containsSparsePath(const NiBlockPathView& query) const noexcept {

        if (query.empty() == 0)
            return true;

        if (query._length > _length)
            return false;

        // scan through nodes[], advancing `need` each time we match
        std::size_t need = 0;
        for (std::size_t i = 0; i < _length && need < query._length; ++i) {
            if (_nodes[i] == query._nodes[need]) {
                ++need;
            }
        }
        return (need == query._length);
    }

    // checks if `query` (as a NiBlockPath) appears as a consecutive segment subsequence
    bool contains(const NiBlockPath& query) const noexcept {

        if (query.empty() || query._length > _length) return false;
        // slide a window of size query.length
        uint32_t window = query._length;
        for (uint32_t start = 0; start + window <= _length; ++start) {
            if (std::memcmp(&_nodes[start], query._nodes, window * sizeof(NiFixedString)) == 0)
                return true;
        }
        return false;

    }

    bool contains(const char* query) const noexcept {
        if (!query || !*query) return false;
        return raw.find(query) != std::string::npos;
    }

    //Full‐path equality
    bool operator==(const NiBlockPath& rhs) const noexcept {
        if (_length != rhs._length) return false;
        return std::memcmp(_nodes, rhs._nodes, _length * sizeof(NiFixedString)) == 0;
    }

};