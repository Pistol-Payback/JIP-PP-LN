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

//For portability
#ifdef _MSC_VER
#include <malloc.h>
#define ALLOCA _alloca
#else
#include <alloca.h>
#define ALLOCA alloca
#endif
#include "p_plus/pSmallBuffer.hpp"

//template<typename U>
//concept PathPart = std::same_as<U, NiFixedString> || std::same_as<U, NiBlockPathBase>;

struct NiRuntimeNodeVector;

template<typename U>
concept PathPart =
std::same_as<std::decay_t<U>, NiFixedString>       // single segment
|| std::convertible_to<U, const char*>               // C‑strings
|| std::derived_from<std::decay_t<U>, NiBlockPathBase>;

struct IndexLinkedList {

    IndexLinkedList(NiFixedString childObj) : childObj(childObj) {}

    UInt16 previous;
    UInt16 next;
    NiFixedString childObj;

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

    inline std::string reverseFormat(TESForm*& form) const {
        form = LookupFormByRefID(refID);
        if (suffix.isValid() == false) {
            return "";
        }
        return "*" + suffix.ToString() + "*";
    }

    bool operator==(const RefModel& other) const noexcept {
        return refID == other.refID && suffix == other.suffix;
    }

    bool operator!=(const RefModel& other) const noexcept {
        return !(*this == other);
    }

    inline ModelTemp getRuntimeModel() const {

        if (TESForm* form = LookupFormByRefID(refID)) {
            return getRuntimeModel(form);
        }

        return ModelTemp();

    }

    //Not sure if its a good idea to let RefModel handle all this logic
    inline ModelTemp getRuntimeModel(TESForm* form) const {

        const char* modelPath = form->GetModelPath();
        if (!modelPath || !modelPathExists(modelPath)) {
            return ModelTemp();
        }

        ModelTemp tempModel(modelPath);

        if (!tempModel.clonedNode) {
            return ModelTemp();
        }
        tempModel.clonedNode->attachAllRuntimeNodes(form->getRuntimeNodes());
        tempModel.clonedNode->AddSuffixToAllChildren(suffix.getPtr());
        tempModel.clonedNode->RemoveCollision(); //Prevents invalid collision
        tempModel.clonedNode->DownwardsInitPointLights(tempModel.clonedNode);
        tempModel.clonedNode->AddPointLights();

        return tempModel;

    }

    inline ModelTemp getRuntimeModel(NiRuntimeNodeVector*& resultVector) const {

        if (TESForm* form = LookupFormByRefID(refID)) {
            const char* modelPath = form->GetModelPath();
            if (!modelPath || !modelPathExists(modelPath)) {
                return ModelTemp();
            }
            ModelTemp tempModel{};
            resultVector = form->getRuntimeNodes();
            tempModel.filePath = modelPath;
            return tempModel;
        }

        return ModelTemp();

    }

    void reverseFormatTo(pSmallBufferWriter& buf, TESForm*& outForm) const noexcept {
        outForm = LookupFormByRefID(refID);
        if (suffix.isValid()) {
            buf.pushChar('*');
            buf.appendFixed(suffix);
            buf.pushChar('*');
        }
    }

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

    ModelPath(const ModelPath& pth) noexcept
        : suffix(pth.suffix)
        , path(pth.path)
    {
    }

    bool operator==(const ModelPath& other) const noexcept {
        return path == other.path && suffix == other.suffix;
    }

    bool operator!=(const ModelPath& other) const noexcept {
        return !(*this == other);
    }

    void reverseFormatTo(pSmallBufferWriter& buf) const noexcept {
        if (suffix.isValid()) {
            buf.pushChar('*');
            buf.appendFixed(suffix);
            buf.pushChar('*');
        }
        buf.appendFixed(path);
    }

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

    explicit NiToken(ModelPath&& modelPath) noexcept(std::is_nothrow_move_constructible_v<ModelPath>)
        : data(std::in_place_type<ModelPath>, std::move(modelPath)) {
    }

    explicit NiToken(const ModelPath& modelPath) noexcept
        : data(modelPath) {
    }

    explicit NiToken(RefModel&& refModel) noexcept(std::is_nothrow_move_constructible_v<RefModel>)
        : data(std::in_place_type<RefModel>, std::move(refModel)) {
    }

    explicit NiToken(const RefModel& refModel) noexcept
        : data(refModel) {
    }

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

    constexpr NiBlockPathView(const NiFixedString* nodes, uint32_t length) noexcept
        : _nodes(const_cast<NiFixedString*>(nodes))
        , _length(length)
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

    // for searching existing blocks. we can check the ref count to make sure full path exists
    bool pathExists() const noexcept {
        for (auto& seg : *this) {
            if (seg.refCount() <= 1)
                return false;
        }
        return true;
    }

    //Suffix-match, checks if the end half of the array matches the entier query.
    bool containsPathSuffix(const NiBlockPathView& query) const noexcept {
        if (query._length > _length) return false;
        uint32_t off = _length - query._length;
        // pointer-compare is fine because equal strings are interned to identical pointers
        return std::memcmp(&_nodes[off], query._nodes, query._length * sizeof(NiFixedString)) == 0;
    }

    bool containsSparsePath(const NiBlockPathView& query) const noexcept {

        if (query.empty())
            return true;

        if (query._length > _length)
            return false;

        // scan through nodes[], advancing `need` each time we match
        std::size_t need = 0;
        for (std::size_t i = 0; i < _length && need < query._length; ++i) {
            auto base = _nodes[i];
            auto toFind = query._nodes[need];
            if (base == toFind) {
                ++need;
            }
        }
        return (need == query._length);
    }

    bool contains(const NiBlockPathView& query) const noexcept {

        if (query.empty() || query._length > _length) return false;
        // slide a window of size query.length
        uint32_t window = query._length;
        for (uint32_t start = 0; start + window <= _length; ++start) {
            if (std::memcmp(&_nodes[start], query._nodes, window * sizeof(NiFixedString)) == 0)
                return true;
        }
        return false;

    }

    bool contains(const NiBlockPathBuilder& query) const noexcept;

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
    friend NiBlockPathStatic;

};


struct NiBlockPathBase : public NiBlockPathView {

public:
    // default: empty
    NiBlockPathBase() noexcept
        : NiBlockPathView()
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

    // --- new: zero out & free storage ---
    void clear() noexcept {
        for (uint32_t i = 0; i < _length; ++i)
            _nodes[i].~NiFixedString();
        ::operator delete[](_nodes);
        _nodes = nullptr;
        _length = 0;
    }

    NiBlockPathBase& operator=(NiBlockPathBase&& other) noexcept {

        if (this != &other) {

            // destroy our own contents
            clear();

            // then steal
            _nodes = other._nodes;
            _length = other._length;
            other._nodes = nullptr;
            other._length = 0;
        }
        return *this;

    }

    template<PathPart... Parts>
    NiBlockPathBase(Parts&&... parts) : NiBlockPathView(static_cast<NiFixedString*>(nullptr), totalSegments(parts...))
    {
        if (_length == 0) return;
        // raw allocate
        _nodes = static_cast<NiFixedString*>(::operator new[](_length * sizeof(NiFixedString)));

        uint32_t idx = 0;
        // fold‑expression to append each “part”
        (appendPart(std::forward<Parts>(parts), idx), ...);
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

    ~NiBlockPathBase() {
        clear();
    }

    [[nodiscard]] NiBlockPathBase createCopy() const {
        NiBlockPathBase out;
        out._length = _length;

        if (out._length == 0) {
            out._nodes = nullptr;
            return out;
        }

        out._nodes = static_cast<NiFixedString*>(
            ::operator new[](out._length * sizeof(NiFixedString))
            );

        // NiFixedString copy ctor just increments refcount
        for (uint32_t i = 0; i < out._length; ++i)
            new (&out._nodes[i]) NiFixedString(_nodes[i]);

        return out;
    }

    [[nodiscard]] static NiBlockPathBase createCopy(const NiBlockPathView& view) {
        NiBlockPathBase out;
        out._length = view._length;

        if (out._length == 0) {
            out._nodes = nullptr;
            return out;
        }

        out._nodes = static_cast<NiFixedString*>(
            ::operator new[](out._length * sizeof(NiFixedString))
            );

        const NiFixedString* src = view._nodes;
        for (uint32_t i = 0; i < out._length; ++i)
            new (&out._nodes[i]) NiFixedString(src[i]);

        return out;
    }

    inline void appendSparsePath(pSmallBufferWriter& buf) const noexcept {

        for (const auto& seg : *this) {
            buf.pushChar('\\');
            buf.appendFixed(seg);
        }

    }

protected:

    //–– compile‑time segment counting ––
    static constexpr uint32_t segCount(const char* s) noexcept {
        if (!s || !*s) return 0;
        uint32_t c = 1;
        for (auto p = s; *p; ++p)
            if (*p == '\\') ++c;
        return c;
    }
    static constexpr uint32_t segCount(const NiFixedString&) noexcept { return 1; }
    static constexpr uint32_t segCount(const NiBlockPathBase& bp) noexcept { return bp._length; }

    template<typename First, typename... Rest>
    static constexpr uint32_t totalSegments(First&& f, Rest&&... r) noexcept {
        return segCount(std::forward<First>(f))
            + totalSegments(std::forward<Rest>(r)...);
    }
    static constexpr uint32_t totalSegments() noexcept { return 0; }

    //–– run‑time appending ––

    // C-string: split on ‘\’ into segments
    void appendPart(const char* s, uint32_t& idx) {
        if (!s || !*s) return;
        const char* start = s;
        for (const char* p = s; ; ++p) {
            if (*p == '\\' || *p == '\0') {
                size_t len = p - start;
                // carve out len+1 bytes on the stack
                char* buf = static_cast<char*>(ALLOCA(len + 1));
                memcpy(buf, start, len);
                buf[len] = '\0';
                new(&_nodes[idx++]) NiFixedString(buf);
                if (*p == '\0') break;
                start = p + 1;
            }
        }
    }

    void appendPart(const NiFixedString& fs, uint32_t& idx) {
        new(&_nodes[idx++]) NiFixedString(fs);
    }

    void appendPart(const NiBlockPathBase& bp, uint32_t& idx) {
        for (uint32_t i = 0; i < bp._length; ++i)
            new(&_nodes[idx++]) NiFixedString(bp._nodes[i]);
    }

};

struct NiBlockPathStatic : public NiBlockPathBase {

    using NiBlockPathBase::contains;
    using NiBlockPathBase::containsPathSuffix;
    using NiBlockPathBase::containsSparsePath;


    //Raw might not be needed anymore, waste of space.
    //Original Path
    std::string raw; //for fast comparision lookups, so we dont need to create a bunch of NiFixedStrings when comparing a const char* path

    //–– ctors / dtor ––
    NiBlockPathStatic() noexcept
        : NiBlockPathBase()
        , raw()
    {}

    // move‐ctor
    NiBlockPathStatic(NiBlockPathStatic&& other) noexcept
        : NiBlockPathBase(std::move(other))   // steals nodes & length
        , raw(std::move(other.raw))           // steals the string
    {}

    template<typename It>
    NiBlockPathStatic(It begin, It end) : NiBlockPathBase(begin, end), raw()
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
    NiBlockPathStatic(const char* pathString) : NiBlockPathBase(), raw(pathString ? pathString : "")
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

    ~NiBlockPathStatic() = default;

    [[nodiscard]] NiBlockPathStatic createCopy() const {

        NiBlockPathStatic out;
        out._length = _length;
        if (out._length == 0) {
            out._nodes = nullptr;
            out.raw.clear();
            return out;
        }

        // allocate nodes
        out._nodes = static_cast<NiFixedString*>(
            ::operator new[](out._length * sizeof(NiFixedString))
            );

        // Copy-construct NiFixedString (bumps refcounts)
        for (uint32_t i = 0; i < out._length; ++i)
            new (&out._nodes[i]) NiFixedString(_nodes[i]);

        // raw is a simple string copy
        out.raw = raw;
        return out;
    }

    [[nodiscard]] static NiBlockPathStatic createCopy(const NiBlockPathView& view) {

        NiBlockPathStatic out;
        out._length = view._length;
        if (out._length == 0) {
            out._nodes = nullptr;
            out.raw.clear();
            return out;
        }

        // allocate nodes
        out._nodes = static_cast<NiFixedString*>(
            ::operator new[](out._length * sizeof(NiFixedString))
            );

        // Copy-construct from view’s nodes
        const NiFixedString* src = view._nodes;
        for (uint32_t i = 0; i < out._length; ++i)
            new (&out._nodes[i]) NiFixedString(src[i]);

        // reconstruct raw from nodes
        // (avoids needing a raw in the view)
        {
            out.raw.clear();
            out.raw.reserve(
                (out._length ? out._length - 1 : 0) +
                std::accumulate(src, src + out._length, size_t(0),
                    [](size_t acc, const NiFixedString& s) { return acc + s.Length(); })
            );
            for (uint32_t i = 0; i < out._length; ++i) {
                if (i) out.raw.push_back('\\');
                out.raw.append(src[i].CStr());
            }
        }

        return out;
    }

    // disallow copy
    NiBlockPathStatic(const NiBlockPathStatic&) = delete;
    NiBlockPathStatic& operator=(const NiBlockPathStatic&) = delete;

    //Only allow move
    NiBlockPathStatic& operator=(NiBlockPathStatic&& other) noexcept {
        if (this != &other) {
            NiBlockPathBase::operator=(std::move(other));
            raw = std::move(other.raw);
        }
        return *this;
    }

    NiFixedString& operator[](uint32_t i) { return _nodes[i]; }
    const NiFixedString& operator[](uint32_t i) const { return _nodes[i]; }

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

    bool contains(const char* query) const noexcept {
        if (!query || !*query) return false;
        return raw.find(query) != std::string::npos;
    }

    //Full‐path equality
    bool operator==(const NiBlockPathStatic& rhs) const noexcept {
        if (_length != rhs._length) return false;
        return std::memcmp(_nodes, rhs._nodes, _length * sizeof(NiFixedString)) == 0;
    }

    void insertSegment(size_t idx, const NiFixedString& seg) {

        // clamp idx
        if (idx > _length) idx = _length;

        // update `raw` string:
        // find byte position in raw where to splice in "seg\\"
        if (raw.empty()) {
            raw = seg.CStr();
        }
        else {
            size_t pos = 0;
            // Locate byte position in raw where to splice "\\seg"
            if (idx > 0) {
                size_t count = 0;
                while (count < idx) {
                    size_t next = raw.find('\\', pos);
                    if (next == std::string::npos) {
                        pos = raw.size();
                        break;
                    }
                    pos = next + 1;
                    ++count;
                }

                if (pos > 0 && idx < _length) --pos;
            }
            raw.insert(pos, "\\" + std::string(seg.CStr()));
        }

        //rebuild the nodes array with one extra slot:
        auto newArr = static_cast<NiFixedString*>(
            ::operator new[]((_length + 1) * sizeof(NiFixedString))
        );
        // copy [0 .. idx)
        for (size_t i = 0; i < idx; ++i)
            new(&newArr[i]) NiFixedString(_nodes[i]);
        // insert the new segment
        new(&newArr[idx]) NiFixedString(seg);
        // copy [idx .. oldLength)
        for (size_t i = idx; i < _length; ++i)
            new(&newArr[i + 1]) NiFixedString(_nodes[i]);

        // destroy old nodes & swap in the new array
        for (size_t i = 0; i < _length; ++i)
            _nodes[i].~NiFixedString();
        ::operator delete[](_nodes);

        _nodes = newArr;
        ++_length;

    }

    void replaceSegment(size_t idx, const NiFixedString& newSeg) {
        if (idx >= _length) return;
        // replace in-place
        _nodes[idx].~NiFixedString();
        new(&_nodes[idx]) NiFixedString(newSeg);
        // rebuild raw from scratch
        raw.clear();
        for (size_t i = 0; i < _length; ++i) {
            if (i) raw.push_back('\\');
            raw.append(_nodes[i].CStr());
        }
    }

    void removeSegment(size_t idx) {
        if (idx >= _length) return;

        // 1) Update `raw`
        if (_length == 1) {
            raw.clear();
        }
        else {
            // find byte‐offset in raw of the segment
            size_t start = 0, end = raw.size();
            // locate start
            for (size_t i = 0; i < idx; ++i) {
                start = raw.find('\\', start);
                if (start == std::string::npos) return;
                ++start;
            }
            // locate end
            end = raw.find('\\', start);
            if (end == std::string::npos) end = raw.size();
            // also eat the separator if it’s not the last segment
            if (end != raw.size()) ++end;
            raw.erase(start, end - start);
        }

        // 2) Rebuild the _nodes array with one fewer slot
        auto newArr = static_cast<NiFixedString*>(
            ::operator new[]((_length - 1) * sizeof(NiFixedString))
            );
        // copy [0..idx)
        for (size_t i = 0; i < idx; ++i)
            new(&newArr[i]) NiFixedString(_nodes[i]);
        // copy [idx+1..oldLen)
        for (size_t i = idx + 1; i < _length; ++i)
            new(&newArr[i - 1]) NiFixedString(_nodes[i]);

        // destroy old & swap
        for (size_t i = 0; i < _length; ++i)
            _nodes[i].~NiFixedString();
        ::operator delete[](_nodes);
        _nodes = newArr;
        --_length;
    }

};