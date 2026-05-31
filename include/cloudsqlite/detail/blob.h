#pragma once

#include <sqlite3.h>

#include <cstddef>
#include <memory>
#include <utility>

namespace cloudsqlite {
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

/// Allocate a new blob of the given size via sqlite3_malloc64.
inline BlobHandle alloc_blob(int size) {
    void* p = sqlite3_malloc64(static_cast<sqlite3_uint64>(size));
    if (!p) throw std::bad_alloc();
    return BlobHandle(p);
}

} // namespace detail
} // namespace cloudsqlite
