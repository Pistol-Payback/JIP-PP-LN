#pragma once
#include <cstdint>
#include <type_traits>
#include <string>
#include <bit>

constexpr uint32_t BIT(unsigned n) { return 1u << n; }
constexpr uint8_t  BIT8(unsigned n) { return static_cast<uint8_t>(1u << n); }

// Template base class for bitmasks.
template <typename T = uint32_t>
struct pBitMask {

    static_assert(std::is_unsigned_v<T>, "pBitMask<T>: T must be an unsigned integer type");

    T value;

    // Default constructor initializes with zero.
    constexpr pBitMask() : value(0) {}

    // Construct from an underlying value.
    constexpr explicit pBitMask(T v) : value(v) {}

    template <typename... Flags,
    typename = std::enable_if_t<(sizeof...(Flags) >= 1) && (std::conjunction_v<std::is_convertible<Flags, T>...>)>>
    constexpr explicit pBitMask(Flags... flags) : value((T(0) | ... | static_cast<T>(flags))) { }

    constexpr pBitMask(std::initializer_list<T> flags) : value(0) {
        for (T f : flags) value |= f;
    }

    // Overwrite entire mask
    constexpr pBitMask& operator=(T v) {
        value = v;
        return *this;
    }
    constexpr void assign(T v) { value = v; }

    // Implicit conversion to the underlying type.
    constexpr operator T() const { return value; }

    constexpr pBitMask& clear() noexcept { value = 0; return *this; }

    // Sets the bits specified in each flag (variadic parameters).
    template<typename... Flags,
    typename = std::enable_if_t<(std::conjunction_v<std::is_convertible<Flags, T>...>)>>
    constexpr pBitMask& set(Flags... flags) noexcept {
        value |= (T(flags) | ...);
        return *this;
    }

    // Removes (clears) the bits specified in the flags.
    template<typename... Flags,
    typename = std::enable_if_t<(std::conjunction_v<std::is_convertible<Flags, T>...>)>>
    constexpr pBitMask& remove(Flags... flags) noexcept {
        value &= ~( (T(flags) | ...) );
        return *this;
    }

    // Checks whether **all** specified flags are set.
    template<typename... Flags,
    typename = std::enable_if_t<(std::conjunction_v<std::is_convertible<Flags, T>...>)>>
    [[nodiscard]] constexpr bool hasAll(Flags... flags) const noexcept {
        const T mask = (T(flags) | ...);
        return (value & mask) == mask;
    }

    // Checks whether **any** of the specified flags is set.
    template<typename... Flags,
    typename = std::enable_if_t<(std::conjunction_v<std::is_convertible<Flags, T>...>)>>
    [[nodiscard]] constexpr bool hasAny(Flags... flags) const noexcept {
        const T mask = (T(flags) | ...);
        return (value & mask) != 0;
    }

    [[nodiscard]] constexpr bool none() const noexcept { return value == 0; }
    [[nodiscard]] constexpr int  count() const noexcept {
        #if __cpp_lib_bitops
            if constexpr (std::is_same_v<T, uint32_t>) return std::popcount(value);
            if constexpr (std::is_same_v<T, uint64_t>) return std::popcount(value);
        #endif
        // Fallback: Kernighan loop
        T v = value; int c = 0; while (v) { v &= (v - 1); ++c; } return c;
    }

    // Write a single flag on or off
    constexpr pBitMask& write(T flag, bool on) noexcept {
        return on ? set(flag) : remove(flag);
    }

    constexpr pBitMask& toggle(T flag) noexcept {
        value ^= flag; return *this;
    }

    std::string ToString() const {
        std::string bits;
        bits.reserve(sizeof(T) * 8);
        for (int b = sizeof(T) * 8 - 1; b >= 0; --b) {
            bits.push_back((value & (T(1) << b)) ? '1' : '0');
        }
        return bits;
    }

    // bitwise operators (value-preserving)
    [[nodiscard]] friend constexpr pBitMask operator|(pBitMask a, pBitMask b) noexcept { return pBitMask{T(a.value | b.value)}; }
    [[nodiscard]] friend constexpr pBitMask operator&(pBitMask a, pBitMask b) noexcept { return pBitMask{T(a.value & b.value)}; }
    [[nodiscard]] friend constexpr pBitMask operator^(pBitMask a, pBitMask b) noexcept { return pBitMask{T(a.value ^ b.value)}; }
    [[nodiscard]] friend constexpr pBitMask operator~(pBitMask a) noexcept { return pBitMask{T(~a.value)}; }

    constexpr pBitMask& operator|=(pBitMask rhs) noexcept { value |= rhs.value; return *this; }
    constexpr pBitMask& operator&=(pBitMask rhs) noexcept { value &= rhs.value; return *this; }
    constexpr pBitMask& operator^=(pBitMask rhs) noexcept { value ^= rhs.value; return *this; }

    // comparisons
    [[nodiscard]] friend constexpr bool operator==(pBitMask a, pBitMask b) noexcept { return a.value == b.value; }
    [[nodiscard]] friend constexpr bool operator!=(pBitMask a, pBitMask b) noexcept { return a.value != b.value; }

};