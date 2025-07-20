#include <cstdint>

constexpr uint32_t BIT(unsigned n) { return 1u << n; }
constexpr uint8_t BIT8(unsigned n) { return uint8_t(1u << n); }

// Template base class for bitmasks.
template <typename T = uint32_t>
struct pBitMask {
    T value;

    // Default constructor initializes with zero.
    constexpr pBitMask() : value(0) {}

    // Construct from an underlying value.
    constexpr explicit pBitMask(T v) : value(v) {}

    // Overwrite entire mask
    constexpr pBitMask& operator=(T v) {
        value = v;
        return *this;
    }
    constexpr void assign(T v) { value = v; }

    // Implicit conversion to the underlying type.
    constexpr operator T() const { return value; }

    // Sets the bits specified in each flag (variadic parameters).
    template<typename... Flags>
    constexpr void set(Flags... flags) {
        // Fold expression (requires C++17) ORs all provided flag values into one mask.
        value |= (flags | ...);
    }

    // Removes (clears) the bits specified in the flags.
    template<typename... Flags>
    constexpr void remove(Flags... flags) {
        value &= ~(flags | ...);
    }

    // Checks whether **all** specified flags are set.
    template<typename... Flags>
    [[nodiscard]] constexpr bool has(Flags... flags) const {
        T mask = (flags | ...);
        return (value & mask) == mask;
    }

    // Checks whether **any** of the specified flags is set.
    template<typename... Flags>
    [[nodiscard]] constexpr bool hasEither(Flags... flags) const {
        T mask = (flags | ...);
        return (value & mask) != 0;
    }

    // Write a single flag on or off
    constexpr void write(T flag, bool on) {
        if (on) set(flag);
        else   remove(flag);
    }

    std::string ToString() const {
        std::string bits;
        bits.reserve(sizeof(T) * 8);
        for (int b = sizeof(T) * 8 - 1; b >= 0; --b) {
            bits.push_back((value & (T(1) << b)) ? '1' : '0');
        }
        return bits;
    }

};