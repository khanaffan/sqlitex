#include "sqlitex/function.h"
#include "sqlitex/value.h"

#include <sqlite3.h>

#include <cstring>
#include <stdexcept>
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
    sqlite3_result_text(ctx_, val, -1, SQLITE_TRANSIENT);
}

void FunctionContext::set_result(std::string_view val) {
    sqlite3_result_text(ctx_, val.data(), static_cast<int>(val.size()),
                        SQLITE_TRANSIENT);
}

void FunctionContext::set_result(const void* blob, int size) {
    sqlite3_result_blob(ctx_, blob, size, SQLITE_TRANSIENT);
}

void FunctionContext::set_result(const std::vector<uint8_t>& blob) {
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
        set_result(val.as_text());
        break;
    case Value::Type::Blob: {
        auto& blob = val.as_blob();
        set_result(blob.data(), static_cast<int>(blob.size()));
        break;
    }
    }
}

void FunctionContext::set_error(const char* msg) {
    sqlite3_result_error(ctx_, msg, -1);
}

void FunctionContext::set_subtype(unsigned int subtype) {
    sqlite3_result_subtype(ctx_, subtype);
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
        const void* blob = sqlite3_value_blob(val);
        return Value(blob, n);
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
