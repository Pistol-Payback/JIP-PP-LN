struct pSmallBufferWriter {

    static constexpr size_t kCap = 256;

    char   data[kCap];
    size_t len = 0; // number of bytes written (excluding '\0')

    void   reset()      noexcept { len = 0; data[0] = '\0'; }
    bool   empty()      const noexcept { return len == 0; }
    size_t remaining()  const noexcept { return (len < kCap) ? (kCap - 1 - len) : 0; }
    void   seal()       noexcept { data[(len < kCap) ? len : (kCap - 1)] = '\0'; }

    void pushChar(char c) noexcept {
        if (remaining()) data[len++] = c;
        seal();
    }

    void append(const char* s, size_t n) noexcept {
        if (!s || !n) return;
        size_t w = (n <= remaining()) ? n : remaining();
        if (w) std::memcpy(data + len, s, w);
        len += w;
        seal();
    }

    void appendCStr(const char* s) noexcept {
        if (!s) return;
        append(s, std::strlen(s));
    }

    void appendFixed(const NiFixedString& s) noexcept {
        if (const char* p = s.CStr()) appendCStr(p);
    }

    const char* c_str() const noexcept { return data; }

};