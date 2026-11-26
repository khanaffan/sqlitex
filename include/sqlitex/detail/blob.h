#pragma once

#include "sqlitex/sqlitex_export.h"

#include <memory>

namespace sqlitex {
namespace detail {

/// Release a buffer allocated by sqlite3_malloc / sqlite3_malloc64 through the
/// library boundary so shared builds do not require consumers to link sqlite3.
SQLITEX_EXPORT void free_sqlite_memory(void* p) noexcept;

/// Custom deleter that calls sqlite3_free on a pointer.
struct SqliteFreeDeleter {
    void operator()(void* p) const noexcept {
        free_sqlite_memory(p);
    }
};

/// RAII handle for a buffer allocated by sqlite3_malloc / sqlite3_malloc64.
/// Callers must NOT call sqlite3_free directly.
using BlobHandle = std::unique_ptr<void, SqliteFreeDeleter>;

/// Wrap a raw pointer+size from sqlite3_malloc into a BlobHandle.
inline BlobHandle wrap_blob(void* data, int /*size*/) noexcept {
    return BlobHandle(data);
}

} // namespace detail
} // namespace sqlitex
