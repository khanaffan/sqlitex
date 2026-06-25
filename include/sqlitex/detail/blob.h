#pragma once

#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstddef>
#include <memory>

namespace sqlitex {
namespace detail {

/// Custom deleter that calls sqlite3_free on a pointer.
struct SqliteFreeDeleter {
    void operator()(void* p) const noexcept {
        sqlite3_free(p);
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
