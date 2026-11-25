#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace sqlitex {

class Value;

// ============================================================================
// Filesystem path concept (avoids overload ambiguity with std::string_view)
// ============================================================================

template <typename T>
concept FilesystemPath =
    std::same_as<std::remove_cvref_t<T>, std::filesystem::path>;

namespace detail {

inline std::string path_to_utf8(const std::filesystem::path& path) {
    auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

} // namespace detail

// ============================================================================
// Byte range concepts & span conversion helpers
// ============================================================================

/// True for raw byte element types (std::byte or uint8_t).
/// Deliberately excludes char so text is never silently bound as a BLOB.
template <typename T>
concept ByteLike = std::same_as<std::remove_cv_t<T>, std::byte> ||
                   std::same_as<std::remove_cv_t<T>, uint8_t>;

/// Contiguous, sized range of std::byte or uint8_t.
template <typename R>
concept ConstByteRange =
    std::ranges::contiguous_range<R> && std::ranges::sized_range<R> &&
    ByteLike<std::ranges::range_value_t<R>>;

/// Mutable contiguous, sized range of std::byte or uint8_t.
template <typename R>
concept MutableByteRange =
    ConstByteRange<R> &&
    !std::is_const_v<
        std::remove_reference_t<std::ranges::range_reference_t<R>>>;

/// View any ConstByteRange as std::span<const std::byte>.
template <ConstByteRange R>
[[nodiscard]] constexpr std::span<const std::byte> to_const_bytes(
    const R& range) noexcept {
    return std::as_bytes(
        std::span(std::ranges::data(range), std::ranges::size(range)));
}

/// View any MutableByteRange as std::span<std::byte>.
template <MutableByteRange R>
[[nodiscard]] constexpr std::span<std::byte> to_writable_bytes(
    R& range) noexcept {
    return std::as_writable_bytes(
        std::span(std::ranges::data(range), std::ranges::size(range)));
}

// ============================================================================
// Numeric concepts (prevent accidental bool/char/narrowing misuse)
// ============================================================================

template <typename T>
concept CharacterType = std::same_as<std::remove_cv_t<T>, char> ||
                        std::same_as<std::remove_cv_t<T>, signed char> ||
                        std::same_as<std::remove_cv_t<T>, unsigned char> ||
                        std::same_as<std::remove_cv_t<T>, wchar_t> ||
                        std::same_as<std::remove_cv_t<T>, char8_t> ||
                        std::same_as<std::remove_cv_t<T>, char16_t> ||
                        std::same_as<std::remove_cv_t<T>, char32_t>;

/// Integral type suitable for SQL INTEGER binding (excludes bool and chars).
template <typename T>
concept SqlInteger =
    std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool> &&
    !CharacterType<T>;

/// Floating-point type suitable for SQL REAL binding.
template <typename T>
concept SqlFloat = std::floating_point<T>;

// ============================================================================
// Column extraction concepts
// ============================================================================

namespace detail {

template <typename T>
struct IsScalarOwnedColumn : std::false_type {};
template <>
struct IsScalarOwnedColumn<bool> : std::true_type {};
template <>
struct IsScalarOwnedColumn<int32_t> : std::true_type {};
template <>
struct IsScalarOwnedColumn<int64_t> : std::true_type {};
template <>
struct IsScalarOwnedColumn<double> : std::true_type {};
template <>
struct IsScalarOwnedColumn<std::string> : std::true_type {};
template <>
struct IsScalarOwnedColumn<std::vector<std::byte>> : std::true_type {};
template <>
struct IsScalarOwnedColumn<std::vector<uint8_t>> : std::true_type {};
template <>
struct IsScalarOwnedColumn<Value> : std::true_type {};

template <typename T>
struct IsOwnedColumnValue : IsScalarOwnedColumn<T> {};
template <typename U>
struct IsOwnedColumnValue<std::optional<U>> : IsScalarOwnedColumn<U> {};

template <typename T>
struct IsScalarViewColumn : std::false_type {};
template <>
struct IsScalarViewColumn<std::string_view> : std::true_type {};
template <>
struct IsScalarViewColumn<std::span<const std::byte>> : std::true_type {};

template <typename T>
struct IsViewColumnValue : IsScalarViewColumn<T> {};
template <typename U>
struct IsViewColumnValue<std::optional<U>> : IsScalarViewColumn<U> {};

/// Typed C destructor callback compatible with SQLite void(*)(void*) hooks.
template <typename T>
void delete_typed(void* p) noexcept {
    delete static_cast<T*>(p);
}

} // namespace detail

/// Types safe to return across statement destruction (e.g.
/// Database::query_value). Deliberately excludes non-owning views like
/// std::string_view and std::span.
template <typename T>
concept OwnedColumnValue =
    detail::IsOwnedColumnValue<std::remove_cvref_t<T>>::value;

/// Types supported by Statement::column_as<T>() and Statement::rows<Ts...>().
template <typename T>
concept ColumnValue = OwnedColumnValue<T> ||
                      detail::IsViewColumnValue<std::remove_cvref_t<T>>::value;

// ============================================================================
// Parameter binding concept
// ============================================================================

namespace detail {

template <typename T>
struct IsBindableScalar : std::false_type {};
template <>
struct IsBindableScalar<std::nullptr_t> : std::true_type {};
template <>
struct IsBindableScalar<std::nullopt_t> : std::true_type {};
template <>
struct IsBindableScalar<bool> : std::true_type {};
template <>
struct IsBindableScalar<Value> : std::true_type {};

template <typename T>
concept BindableCore =
    IsBindableScalar<std::remove_cvref_t<T>>::value ||
    SqlInteger<std::remove_cvref_t<T>> || SqlFloat<std::remove_cvref_t<T>> ||
    std::convertible_to<T, std::string_view> ||
    std::same_as<std::remove_cvref_t<T>, const char*> ||
    std::same_as<std::remove_cvref_t<T>, char*> ||
    ConstByteRange<std::remove_cvref_t<T>>;

template <typename T>
struct IsBindable : std::bool_constant<BindableCore<T>> {};
template <typename U>
struct IsBindable<std::optional<U>> : std::bool_constant<BindableCore<U>> {};

} // namespace detail

/// Types accepted by Statement::bind() / Statement::bind_all() / Database
/// parameterized queries.
template <typename T>
concept Bindable = detail::IsBindable<std::remove_cvref_t<T>>::value;

} // namespace sqlitex
