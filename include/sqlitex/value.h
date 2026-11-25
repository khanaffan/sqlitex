#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sqlitex {

// ============================================================================
// Value — a variant representing any SQLite value
// ============================================================================

class SQLITEX_EXPORT Value {
    public:
    /// SQLite storage classes.
    enum class Type {
        Integer,
        Float,
        Text,
        Blob,
        Null,
    };

    // --- Constructors ---

    Value() noexcept : data_(std::monostate{}) {
    }

    Value(std::nullptr_t) noexcept : data_(std::monostate{}) {
    }

    explicit Value(int32_t v) noexcept : data_(static_cast<int64_t>(v)) {
    }
    explicit Value(int64_t v) noexcept : data_(v) {
    }
    explicit Value(double v) noexcept : data_(v) {
    }
    explicit Value(std::string v) : data_(std::move(v)) {
    }
    explicit Value(std::string_view v) : data_(std::string(v)) {
    }
    // Construct from a null-terminated C string.  Passing nullptr is safe
    // (treated as empty string).  The constructor is explicit to prevent
    // accidental implicit conversions from const char*.
    explicit Value(const char* v) : data_(std::string(v != nullptr ? v : "")) {
    }

    explicit Value(const void* blob, int size)
        : data_(
              std::vector<uint8_t>(static_cast<const uint8_t*>(blob),
                                   static_cast<const uint8_t*>(blob) + size)) {
    }

    explicit Value(std::vector<uint8_t> blob) : data_(std::move(blob)) {
    }

    // --- Type inspection ---

    Type type() const noexcept {
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

    bool is_null() const noexcept {
        return type() == Type::Null;
    }
    bool is_integer() const noexcept {
        return type() == Type::Integer;
    }
    bool is_float() const noexcept {
        return type() == Type::Float;
    }
    bool is_text() const noexcept {
        return type() == Type::Text;
    }
    bool is_blob() const noexcept {
        return type() == Type::Blob;
    }

    // --- Accessors ---

    int64_t get_int() const {
        return std::get<int64_t>(data_);
    }

    double get_double() const {
        if (auto* p = std::get_if<double>(&data_))
            return *p;
        if (auto* p = std::get_if<int64_t>(&data_))
            return static_cast<double>(*p);
        throw std::bad_variant_access();
    }

    std::string_view get_text() const {
        return std::get<std::string>(data_);
    }

    const std::vector<uint8_t>& get_blob() const {
        return std::get<std::vector<uint8_t>>(data_);
    }

    const void* blob_data() const {
        return get_blob().data();
    }

    int blob_size() const {
        return static_cast<int>(get_blob().size());
    }

    // --- Convenience: get_as<T>() ---

    int64_t as_int64() const;
    double as_double() const;
    const std::vector<uint8_t>& as_blob() const;

    /// Convert to a text view.
    ///
    /// For Text values the returned view is backed by the internal std::string
    /// and is valid until this Value is modified or destroyed.
    ///
    /// \warning For Integer and Float values the method uses a thread-local
    /// buffer to format the number.  The returned view is valid only until the
    /// next call to as_text() on **any** Integer or Float Value on the same
    /// thread.  If you need a stable copy, call std::string(v.as_text()) or
    /// use as_int64() / as_double() directly.
    ///
    /// Throws std::bad_variant_access if the value is Null or Blob.
    std::string_view as_text() const;

    // --- Factory from sqlite3_value* ---

    static Value from_sqlite3_value(sqlite3_value* val);

    // --- Comparison ---

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const {
        return !(*this == other);
    }

    private:
    std::variant<std::monostate, int64_t, double, std::string,
                 std::vector<uint8_t>>
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

inline std::string_view Value::as_text() const {
    if (auto* p = std::get_if<std::string>(&data_)) {
        return *p;
    }
    if (auto* p = std::get_if<int64_t>(&data_)) {
        thread_local std::string buf;
        buf = std::to_string(*p);
        return buf;
    }
    if (auto* p = std::get_if<double>(&data_)) {
        thread_local std::string buf;
        buf = std::to_string(*p);
        return buf;
    }
    throw std::bad_variant_access();
}
inline const std::vector<uint8_t>& Value::as_blob() const {
    return get_blob();
}

inline bool Value::operator==(const Value& other) const {
    return data_ == other.data_;
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

    /// SQLite storage class constant (SQLITE_INTEGER, SQLITE_FLOAT, etc.).
    int type() const noexcept;

    /// Best-fit numeric type (sqlite3_value_numeric_type).
    int numeric_type() const noexcept;

    /// Text encoding constant (SQLITE_UTF8, etc.).
    int encoding() const noexcept;

    /// Subtype attached via sqlite3_result_subtype().
    unsigned int subtype() const noexcept;

    /// True if column value is unchanged in an UPDATE on a virtual table.
    bool nochange() const noexcept;

    /// True if value originated from a bound parameter.
    bool from_bind() const noexcept;

    bool is_null() const noexcept;
    int32_t as_int() const noexcept;
    int64_t as_int64() const noexcept;
    double as_double() const noexcept;
    std::string_view as_text() const noexcept;
    const void* as_blob() const noexcept;
    int bytes() const noexcept;

    /// Retrieve an opaque pointer bound/returned with `type_name`
    /// (sqlite3_value_pointer). Returns nullptr if type does not match.
    void* pointer(const char* type_name) const noexcept;

    /// Convert to an owned Value copy.
    Value to_value() const;

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

    ValueRef ref() const noexcept {
        return ValueRef(val_);
    }

    Value to_value() const;

    private:
    sqlite3_value* val_ = nullptr;
};

} // namespace sqlitex
