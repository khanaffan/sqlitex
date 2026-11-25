#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

namespace sqlitex {

// ============================================================================
// BitmaskEnum concept & type-safe Flags<E> wrapper (C++23)
// ============================================================================

/// Specialize to std::true_type for scoped enums that represent bitmask flags.
template <typename E>
struct EnableBitmaskOperators : std::false_type {};

template <typename E>
inline constexpr bool enable_bitmask_operators_v =
    EnableBitmaskOperators<E>::value;

template <typename E>
concept BitmaskEnum = std::is_enum_v<E> && enable_bitmask_operators_v<E>;

/// Strongly-typed bitmask wrapper over a BitmaskEnum.
/// Prevents accidentally mixing flags from different enums or passing raw ints.
template <BitmaskEnum E>
class Flags {
    public:
    using enum_type = E;
    using underlying_type = std::underlying_type_t<E>;

    constexpr Flags() noexcept = default;
    constexpr Flags(E flag) noexcept : bits_(std::to_underlying(flag)) {
    }

    /// Explicit construction from a raw underlying bitmask.
    [[nodiscard]] static constexpr Flags from_raw(
        underlying_type raw) noexcept {
        Flags f;
        f.bits_ = raw;
        return f;
    }

    [[nodiscard]] constexpr underlying_type raw() const noexcept {
        return bits_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return bits_ == 0;
    }

    constexpr explicit operator bool() const noexcept {
        return bits_ != 0;
    }

    [[nodiscard]] constexpr bool contains(E flag) const noexcept {
        auto mask = std::to_underlying(flag);
        return (bits_ & mask) == mask;
    }

    [[nodiscard]] constexpr bool contains(Flags other) const noexcept {
        return (bits_ & other.bits_) == other.bits_;
    }

    [[nodiscard]] constexpr bool any(Flags other) const noexcept {
        return (bits_ & other.bits_) != 0;
    }

    constexpr Flags& operator|=(Flags rhs) noexcept {
        bits_ = static_cast<underlying_type>(bits_ | rhs.bits_);
        return *this;
    }
    constexpr Flags& operator&=(Flags rhs) noexcept {
        bits_ = static_cast<underlying_type>(bits_ & rhs.bits_);
        return *this;
    }
    constexpr Flags& operator^=(Flags rhs) noexcept {
        bits_ = static_cast<underlying_type>(bits_ ^ rhs.bits_);
        return *this;
    }

    [[nodiscard]] constexpr Flags operator|(Flags rhs) const noexcept {
        return from_raw(static_cast<underlying_type>(bits_ | rhs.bits_));
    }
    [[nodiscard]] constexpr Flags operator&(Flags rhs) const noexcept {
        return from_raw(static_cast<underlying_type>(bits_ & rhs.bits_));
    }
    [[nodiscard]] constexpr Flags operator^(Flags rhs) const noexcept {
        return from_raw(static_cast<underlying_type>(bits_ ^ rhs.bits_));
    }
    [[nodiscard]] constexpr Flags operator~() const noexcept {
        return from_raw(static_cast<underlying_type>(~bits_));
    }

    constexpr bool operator==(const Flags&) const noexcept = default;

    private:
    underlying_type bits_ = 0;
};

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator|(E a, E b) noexcept {
    return Flags<E>(a) | Flags<E>(b);
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator|(E a, Flags<E> b) noexcept {
    return Flags<E>(a) | b;
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator&(E a, E b) noexcept {
    return Flags<E>(a) & Flags<E>(b);
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator&(E a, Flags<E> b) noexcept {
    return Flags<E>(a) & b;
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator^(E a, E b) noexcept {
    return Flags<E>(a) ^ Flags<E>(b);
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator^(E a, Flags<E> b) noexcept {
    return Flags<E>(a) ^ b;
}

template <BitmaskEnum E>
[[nodiscard]] constexpr Flags<E> operator~(E a) noexcept {
    return ~Flags<E>(a);
}

} // namespace sqlitex
