#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sqlitex {

// ============================================================================
// ChangeOperation — row mutation operation (hooks, preupdate, sessions)
// ============================================================================

enum class ChangeOperation : int {
    Insert = SQLITE_INSERT,
    Delete = SQLITE_DELETE,
    Update = SQLITE_UPDATE,
};

// ============================================================================
// Value — a variant representing any SQLite value
// ============================================================================

class SQLITEX_EXPORT Value {
    public:
    /// SQLite storage classes (matching SQLITE_INTEGER..SQLITE_NULL).
    enum class Type : int {
        Integer = SQLITE_INTEGER,
        Float = SQLITE_FLOAT,
        Text = SQLITE3_TEXT,
        Blob = SQLITE_BLOB,
        Null = SQLITE_NULL,
    };

    // --- Constructors ---

    Value() noexcept : data_(std::monostate{}) {
    }

    Value(std::nullptr_t) noexcept : data_(std::monostate{}) {
    }

    explicit Value(bool v) noexcept : data_(static_cast<int64_t>(v ? 1 : 0)) {
    }

    template <SqlInteger T>
    explicit Value(T v) noexcept : data_(static_cast<int64_t>(v)) {
    }

    template <SqlFloat T>
    explicit Value(T v) noexcept : data_(static_cast<double>(v)) {
    }

    explicit Value(std::string v) : data_(std::move(v)) {
    }
    explicit Value(std::string_view v) : data_(std::string(v)) {
    }
    // Construct from a null-terminated C string. Passing nullptr is safe
    // (treated as empty string). Explicit to prevent accidental conversions.
    explicit Value(const char* v) : data_(std::string(v != nullptr ? v : "")) {
    }

    explicit Value(std::vector<std::byte> blob) : data_(std::move(blob)) {
    }

    explicit Value(std::span<const std::byte> blob)
        : data_(std::vector<std::byte>(blob.begin(), blob.end())) {
    }

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::vector<std::byte>> &&
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    explicit Value(const R& blob) : Value(to_const_bytes(blob)) {
    }

    // --- Type inspection ---

    [[nodiscard]] Type type() const noexcept {
        if (std::holds_alternative<std::monostate>(data_))
            return Type::Null;
        if (std::holds_alternative<int64_t>(data_))
            return Type::Integer;
        if (std::holds_alternative<double>(data_))
            return Type::Float;
        if (std::holds_alternative<std::string>(data_))
            return Type::Text;
        return Type::Blob;
    }

    [[nodiscard]] bool is_null() const noexcept {
        return type() == Type::Null;
    }
    [[nodiscard]] bool is_integer() const noexcept {
        return type() == Type::Integer;
    }
    [[nodiscard]] bool is_float() const noexcept {
        return type() == Type::Float;
    }
    [[nodiscard]] bool is_text() const noexcept {
        return type() == Type::Text;
    }
    [[nodiscard]] bool is_blob() const noexcept {
        return type() == Type::Blob;
    }

    // --- Accessors ---

    [[nodiscard]] int64_t get_int() const {
        return std::get<int64_t>(data_);
    }

    [[nodiscard]] double get_double() const {
        if (auto* p = std::get_if<double>(&data_))
            return *p;
        if (auto* p = std::get_if<int64_t>(&data_))
            return static_cast<double>(*p);
        throw std::bad_variant_access();
    }

    [[nodiscard]] std::string_view get_text() const {
        return std::get<std::string>(data_);
    }

    [[nodiscard]] const std::vector<std::byte>& get_blob() const {
        return std::get<std::vector<std::byte>>(data_);
    }

    [[nodiscard]] std::span<const std::byte> as_bytes() const {
        return get_blob();
    }

    // --- Convenience conversions ---

    [[nodiscard]] int64_t as_int64() const;
    [[nodiscard]] double as_double() const;

    /// Convert Text, Integer, or Float to an owned std::string.
    /// Throws std::bad_variant_access if the value is Null or Blob.
    [[nodiscard]] std::string to_string() const;

    // --- Factory from sqlite3_value* ---

    [[nodiscard]] static Value from_sqlite3_value(sqlite3_value* val);

    // --- Comparison (C++20 defaulted == synthesizes !=) ---

    bool operator==(const Value& other) const = default;

    private:
    std::variant<std::monostate, int64_t, double, std::string,
                 std::vector<std::byte>>
        data_;
};

// ============================================================================
// Inline accessor implementations
// ============================================================================

inline int64_t Value::as_int64() const {
    if (auto* p = std::get_if<int64_t>(&data_)) {
        return *p;
    }
    if (auto* p = std::get_if<double>(&data_)) {
        return static_cast<int64_t>(*p);
    }
    throw std::bad_variant_access();
}

inline double Value::as_double() const {
    return get_double();
}

inline std::string Value::to_string() const {
    if (auto* p = std::get_if<std::string>(&data_)) {
        return *p;
    }
    if (auto* p = std::get_if<int64_t>(&data_)) {
        return std::to_string(*p);
    }
    if (auto* p = std::get_if<double>(&data_)) {
        return std::to_string(*p);
    }
    throw std::bad_variant_access();
}

// ============================================================================
// ValueRef — non-owning view of sqlite3_value*
// ============================================================================

class ProtectedValue;

class SQLITEX_EXPORT ValueRef {
    public:
    explicit ValueRef(sqlite3_value* val = nullptr) noexcept : val_(val) {
    }

    explicit operator bool() const noexcept {
        return val_ != nullptr;
    }

    sqlite3_value* handle() const noexcept {
        return val_;
    }

    /// SQLite storage class.
    [[nodiscard]] Value::Type type() const noexcept;

    /// Best-fit numeric storage class (sqlite3_value_numeric_type).
    [[nodiscard]] Value::Type numeric_type() const noexcept;

    /// Text encoding constant (SQLITE_UTF8, etc.).
    [[nodiscard]] int encoding() const noexcept;

    /// Subtype attached via sqlite3_result_subtype().
    [[nodiscard]] unsigned int subtype() const noexcept;

    /// True if column value is unchanged in an UPDATE on a virtual table.
    [[nodiscard]] bool nochange() const noexcept;

    /// True if value originated from a bound parameter.
    [[nodiscard]] bool from_bind() const noexcept;

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] int32_t as_int() const noexcept;
    [[nodiscard]] int64_t as_int64() const noexcept;
    [[nodiscard]] double as_double() const noexcept;
    [[nodiscard]] std::string_view as_text() const noexcept;
    [[nodiscard]] std::span<const std::byte> as_bytes() const noexcept;
    [[nodiscard]] int bytes() const noexcept;

    /// Retrieve an opaque pointer bound/returned with `type_name`
    /// (sqlite3_value_pointer). Returns nullptr if type does not match.
    [[nodiscard]] void* pointer(const char* type_name) const noexcept;

    /// Type-safe pointer retrieval.
    template <typename T>
    [[nodiscard]] T* pointer_as(const char* type_name) const noexcept {
        return static_cast<T*>(pointer(type_name));
    }

    /// Convert to an owned Value copy.
    [[nodiscard]] Value to_value() const;

    /// Duplicate into an owned ProtectedValue (sqlite3_value_dup).
    [[nodiscard]] ProtectedValue dup() const;

    private:
    sqlite3_value* val_ = nullptr;
};

// ============================================================================
// ProtectedValue — RAII wrapper for sqlite3_value_dup() / sqlite3_value_free()
// ============================================================================

class SQLITEX_EXPORT ProtectedValue {
    public:
    ProtectedValue() noexcept = default;
    explicit ProtectedValue(sqlite3_value* owned) noexcept : val_(owned) {
    }
    ~ProtectedValue();

    ProtectedValue(ProtectedValue&& other) noexcept;
    ProtectedValue& operator=(ProtectedValue&& other) noexcept;
    ProtectedValue(const ProtectedValue&) = delete;
    ProtectedValue& operator=(const ProtectedValue&) = delete;

    /// Duplicate a sqlite3_value* (sqlite3_value_dup).
    [[nodiscard]] static ProtectedValue dup(const sqlite3_value* val);
    [[nodiscard]] static ProtectedValue dup(const ValueRef& ref);

    explicit operator bool() const noexcept {
        return val_ != nullptr;
    }

    sqlite3_value* handle() const noexcept {
        return val_;
    }

    [[nodiscard]] ValueRef ref() const noexcept {
        return ValueRef(val_);
    }

    [[nodiscard]] Value to_value() const;

    private:
    sqlite3_value* val_ = nullptr;
};

} // namespace sqlitex
