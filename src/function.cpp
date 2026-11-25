#include "sqlitex/function.h"
#include "sqlitex/error.h"
#include "sqlitex/value.h"

#include <sqlite3.h>

#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sqlitex {

// ============================================================================
// FunctionContext implementation
// ============================================================================

void FunctionContext::set_result() noexcept {
    sqlite3_result_null(ctx_);
}

void FunctionContext::set_result(int32_t val) {
    sqlite3_result_int(ctx_, val);
}

void FunctionContext::set_result(int64_t val) {
    sqlite3_result_int64(ctx_, val);
}

void FunctionContext::set_result(double val) {
    sqlite3_result_double(ctx_, val);
}

void FunctionContext::set_result(const char* val) {
    if (val == nullptr) {
        sqlite3_result_null(ctx_);
    } else {
        sqlite3_result_text(ctx_, val, -1, SQLITE_TRANSIENT);
    }
}

void FunctionContext::set_result(std::string_view val) {
    sqlite3_result_text(ctx_, val.data(), static_cast<int>(val.size()),
                        SQLITE_TRANSIENT);
}

void FunctionContext::set_result(std::span<const std::byte> blob) {
    sqlite3_result_blob(ctx_, blob.data(), static_cast<int>(blob.size()),
                        SQLITE_TRANSIENT);
}

void FunctionContext::set_result(const Value& val) {
    switch (val.type()) {
    case Value::Type::Null:
        set_result();
        break;
    case Value::Type::Integer:
        set_result(val.as_int64());
        break;
    case Value::Type::Float:
        set_result(val.as_double());
        break;
    case Value::Type::Text:
        set_result(val.get_text());
        break;
    case Value::Type::Blob:
        set_result(val.as_bytes());
        break;
    }
}

void FunctionContext::set_error(std::string_view msg) {
    sqlite3_result_error(ctx_, msg.data(), static_cast<int>(msg.size()));
}

void FunctionContext::set_error_code(int code) noexcept {
    sqlite3_result_error_code(ctx_, code);
}

void FunctionContext::set_error_nomem() noexcept {
    sqlite3_result_error_nomem(ctx_);
}

void FunctionContext::set_error_toobig() noexcept {
    sqlite3_result_error_toobig(ctx_);
}

void FunctionContext::set_result_zeroblob(int size) noexcept {
    sqlite3_result_zeroblob(ctx_, size);
}

void FunctionContext::set_result_zeroblob64(uint64_t size) {
    int rc = sqlite3_result_zeroblob64(ctx_, static_cast<sqlite3_uint64>(size));
    if (rc != SQLITE_OK) {
        throw_sqlite_error(rc, sqlite3_context_db_handle(ctx_));
    }
}

void FunctionContext::set_result_static(std::string_view val) noexcept {
    sqlite3_result_text(ctx_, val.data(), static_cast<int>(val.size()),
                        SQLITE_STATIC);
}

void FunctionContext::set_result_static(
    std::span<const std::byte> blob) noexcept {
    sqlite3_result_blob(ctx_, blob.data(), static_cast<int>(blob.size()),
                        SQLITE_STATIC);
}

void FunctionContext::set_result_blob64(std::span<const std::byte> blob,
                                        void (*destructor)(void*)) {
    sqlite3_result_blob64(ctx_, blob.data(),
                          static_cast<sqlite3_uint64>(blob.size()),
                          destructor ? destructor : SQLITE_TRANSIENT);
}

void FunctionContext::set_result_text64(std::string_view text,
                                        unsigned char encoding,
                                        void (*destructor)(void*)) {
    sqlite3_result_text64(ctx_, text.data(),
                          static_cast<sqlite3_uint64>(text.size()),
                          destructor ? destructor : SQLITE_TRANSIENT, encoding);
}

void FunctionContext::set_result_pointer(void* ptr, const char* type_name,
                                         void (*destructor)(void*)) noexcept {
    sqlite3_result_pointer(ctx_, ptr, type_name, destructor);
}

void FunctionContext::set_result_value(const ValueRef& val) noexcept {
    sqlite3_result_value(ctx_, val.handle());
}

void FunctionContext::set_result_value(const ProtectedValue& val) noexcept {
    sqlite3_result_value(ctx_, val.handle());
}

void FunctionContext::set_subtype(unsigned int subtype) {
    sqlite3_result_subtype(ctx_, subtype);
}

void* FunctionContext::get_auxdata(int arg_index) const noexcept {
    return sqlite3_get_auxdata(ctx_, arg_index);
}

void FunctionContext::set_auxdata(int arg_index, void* data,
                                  void (*destructor)(void*)) noexcept {
    sqlite3_set_auxdata(ctx_, arg_index, data, destructor);
}

sqlite3* FunctionContext::db_handle() const noexcept {
    return sqlite3_context_db_handle(ctx_);
}

bool FunctionContext::vtab_nochange() const noexcept {
    return sqlite3_vtab_nochange(ctx_) != 0;
}

// ============================================================================
// ValueRef & ProtectedValue implementations
// ============================================================================

namespace {

Value::Type map_value_type(int raw_type) noexcept {
    switch (raw_type) {
    case SQLITE_INTEGER:
        return Value::Type::Integer;
    case SQLITE_FLOAT:
        return Value::Type::Float;
    case SQLITE3_TEXT:
        return Value::Type::Text;
    case SQLITE_BLOB:
        return Value::Type::Blob;
    case SQLITE_NULL:
    default:
        return Value::Type::Null;
    }
}

} // anonymous namespace

Value::Type ValueRef::type() const noexcept {
    return val_ ? map_value_type(sqlite3_value_type(val_)) : Value::Type::Null;
}

Value::Type ValueRef::numeric_type() const noexcept {
    return val_ ? map_value_type(sqlite3_value_numeric_type(val_))
                : Value::Type::Null;
}

int ValueRef::encoding() const noexcept {
    return val_ ? sqlite3_value_encoding(val_) : SQLITE_UTF8;
}

unsigned int ValueRef::subtype() const noexcept {
    return val_ ? sqlite3_value_subtype(val_) : 0;
}

bool ValueRef::nochange() const noexcept {
    return val_ ? (sqlite3_value_nochange(val_) != 0) : false;
}

bool ValueRef::from_bind() const noexcept {
    return val_ ? (sqlite3_value_frombind(val_) != 0) : false;
}

bool ValueRef::is_null() const noexcept {
    return type() == Value::Type::Null;
}

int32_t ValueRef::as_int() const noexcept {
    return val_ ? sqlite3_value_int(val_) : 0;
}

int64_t ValueRef::as_int64() const noexcept {
    return val_ ? static_cast<int64_t>(sqlite3_value_int64(val_)) : 0;
}

double ValueRef::as_double() const noexcept {
    return val_ ? sqlite3_value_double(val_) : 0.0;
}

std::string_view ValueRef::as_text() const noexcept {
    if (val_ == nullptr) {
        return {};
    }
    const auto* txt = reinterpret_cast<const char*>(sqlite3_value_text(val_));
    if (txt == nullptr) {
        return {};
    }
    return std::string_view(
        txt, static_cast<std::size_t>(sqlite3_value_bytes(val_)));
}

std::span<const std::byte> ValueRef::as_bytes() const noexcept {
    if (val_ == nullptr) {
        return {};
    }
    const auto* blob = static_cast<const std::byte*>(sqlite3_value_blob(val_));
    int const n = sqlite3_value_bytes(val_);
    if (blob == nullptr || n <= 0) {
        return {};
    }
    return {blob, static_cast<std::size_t>(n)};
}

int ValueRef::bytes() const noexcept {
    return val_ ? sqlite3_value_bytes(val_) : 0;
}

void* ValueRef::pointer(const char* type_name) const noexcept {
    return val_ ? sqlite3_value_pointer(val_, type_name) : nullptr;
}

Value ValueRef::to_value() const {
    return Value::from_sqlite3_value(val_);
}

ProtectedValue ValueRef::dup() const {
    return ProtectedValue::dup(val_);
}

ProtectedValue::~ProtectedValue() {
    if (val_ != nullptr) {
        sqlite3_value_free(val_);
    }
}

ProtectedValue::ProtectedValue(ProtectedValue&& other) noexcept
    : val_(std::exchange(other.val_, nullptr)) {
}

ProtectedValue& ProtectedValue::operator=(ProtectedValue&& other) noexcept {
    if (this != &other) {
        if (val_ != nullptr) {
            sqlite3_value_free(val_);
        }
        val_ = std::exchange(other.val_, nullptr);
    }
    return *this;
}

ProtectedValue ProtectedValue::dup(const sqlite3_value* val) {
    if (val == nullptr) {
        return {};
    }
    sqlite3_value* copy = sqlite3_value_dup(val);
    if (copy == nullptr) {
        throw_sqlite_error(SQLITE_NOMEM, nullptr);
    }
    return ProtectedValue(copy);
}

ProtectedValue ProtectedValue::dup(const ValueRef& ref) {
    return dup(ref.handle());
}

Value ProtectedValue::to_value() const {
    return Value::from_sqlite3_value(val_);
}

// ============================================================================
// Value::from_sqlite3_value (declared in value.h)
// ============================================================================

Value Value::from_sqlite3_value(sqlite3_value* val) {
    if (val == nullptr) {
        return {};
    }
    int const type = sqlite3_value_type(val);
    switch (type) {
    case SQLITE_INTEGER:
        return Value(static_cast<int64_t>(sqlite3_value_int64(val)));
    case SQLITE_FLOAT:
        return Value(sqlite3_value_double(val));
    case SQLITE_TEXT: {
        int const n = sqlite3_value_bytes(val);
        const char* text =
            reinterpret_cast<const char*>(sqlite3_value_text(val));
        return Value(std::string(text, static_cast<std::size_t>(n)));
    }
    case SQLITE_BLOB: {
        int const n = sqlite3_value_bytes(val);
        const auto* blob =
            static_cast<const std::byte*>(sqlite3_value_blob(val));
        if (blob == nullptr || n <= 0) {
            return Value(std::vector<std::byte>{});
        }
        return Value(
            std::span<const std::byte>(blob, static_cast<std::size_t>(n)));
    }
    case SQLITE_NULL:
    default:
        return {};
    }
}

// ============================================================================
// WindowFunction default final() — delegates to value()
// ============================================================================

void WindowFunction::final(FunctionContext& ctx) {
    value(ctx);
}

// ============================================================================
// Internal helpers
// ============================================================================

namespace detail {

std::vector<Value> convert_args(int argc, sqlite3_value** argv) {
    std::vector<Value> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(Value::from_sqlite3_value(argv[i]));
    }
    return args;
}

} // namespace detail

} // namespace sqlitex
