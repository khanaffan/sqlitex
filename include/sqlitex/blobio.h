#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Database;

enum class BlobOpenMode : int {
    ReadOnly = 0,
    ReadWrite = 1,
};

// ============================================================================
// BlobIO — RAII wrapper for sqlite3_blob* (Incremental BLOB I/O)
// ============================================================================

class SQLITEX_EXPORT BlobIO {
    public:
    BlobIO() noexcept = default;

    /// Open a blob for incremental I/O.
    BlobIO(Database& db, std::string_view db_name, std::string_view table,
           std::string_view column, int64_t rowid,
           BlobOpenMode mode = BlobOpenMode::ReadOnly);

    ~BlobIO();

    // Move-only
    BlobIO(BlobIO&& other) noexcept;
    BlobIO& operator=(BlobIO&& other) noexcept;
    BlobIO(const BlobIO&) = delete;
    BlobIO& operator=(const BlobIO&) = delete;

    explicit operator bool() const noexcept {
        return blob_ != nullptr;
    }

    sqlite3_blob* handle() noexcept {
        return blob_;
    }
    const sqlite3_blob* handle() const noexcept {
        return blob_;
    }

    // === I/O ===

    /// Read buffer.size() bytes from the blob starting at offset into buffer.
    void read(std::span<std::byte> buffer, int offset);

    template <MutableByteRange R>
        requires(!std::same_as<std::remove_cvref_t<R>, std::span<std::byte>>)
    void read(R& buffer, int offset) {
        read(to_writable_bytes(buffer), offset);
    }

    /// Write data.size() bytes from data into the blob starting at offset.
    void write(std::span<const std::byte> data, int offset);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void write(const R& data, int offset) {
        write(to_const_bytes(data), offset);
    }

    /// Write UTF-8 text bytes into the blob at offset.
    void write(std::string_view text, int offset);

    /// Return the number of bytes in the blob.
    [[nodiscard]] int size() const noexcept;

    /// Reopen the blob handle on a different row of the same table/column.
    void reopen(int64_t rowid);

    // === Convenience ===

    /// Read the entire blob contents into a byte vector.
    [[nodiscard]] std::vector<std::byte> read_all();

    /// Read the entire blob contents as a string.
    [[nodiscard]] std::string read_all_text();

    /// Write bytes at offset 0.
    void write_all(std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void write_all(const R& data) {
        write_all(to_const_bytes(data));
    }

    /// Write text bytes at offset 0.
    void write_all(std::string_view text);

    private:
    sqlite3_blob* blob_ = nullptr;
    Database* db_ = nullptr;
};

} // namespace sqlitex
