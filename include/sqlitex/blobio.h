#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Database;

// ============================================================================
// BlobIO — RAII wrapper for sqlite3_blob* (Incremental BLOB I/O)
// ============================================================================
//
// BlobIO provides random-access read/write to a BLOB value in a row
// identified by its rowid, without loading the entire blob into memory.
// This is the wrapper for SQLite's incremental blob I/O API:
//
//   sqlite3_blob_open  → constructor
//   sqlite3_blob_read  → read()
//   sqlite3_blob_write → write()
//   sqlite3_blob_bytes → size()
//   sqlite3_blob_reopen→ reopen()
//   sqlite3_blob_close → destructor
//
// Example:
//
//   // Open the "data" column of row 42 in table "docs" for writing
//   BlobIO blob(db, "main", "docs", "data", 42, true);
//   blob.write("hello", 5, 0);               // write 5 bytes at offset 0
//   auto data = blob.read_all();             // read entire blob
//   blob.reopen(99);                         // move to a different row
//
// ============================================================================

class SQLITEX_EXPORT BlobIO {
    public:
    /// Default constructor.  The blob is closed (not open).
    BlobIO() noexcept = default;

    /// Open a blob for incremental I/O.
    ///
    /// @param db        Database connection.
    /// @param db_name   Database name ("main", "temp", or an attached
    /// database).
    /// @param table     Table name (must not be a virtual table).
    /// @param column    Column name containing the BLOB.
    /// @param rowid     Rowid of the row.
    /// @param readwrite If false, open read-only; if true, open read/write.
    ///                  Requires the row to exist (cannot extend shorter rows).
    BlobIO(Database& db, std::string_view db_name, std::string_view table,
           std::string_view column, int64_t rowid, bool readwrite = false);

    ~BlobIO();

    // Move-only (sqlite3_blob handles must not be copied)
    BlobIO(BlobIO&& other) noexcept;
    BlobIO& operator=(BlobIO&& other) noexcept;
    BlobIO(const BlobIO&) = delete;
    BlobIO& operator=(const BlobIO&) = delete;

    /// Returns true if the blob handle is open.
    explicit operator bool() const noexcept {
        return blob_ != nullptr;
    }

    /// Raw sqlite3_blob* handle.  nullptr when not open.
    sqlite3_blob* handle() noexcept {
        return blob_;
    }
    const sqlite3_blob* handle() const noexcept {
        return blob_;
    }

    // === I/O ===

    /// Read @p size bytes from the blob starting at @p offset into @p buffer.
    /// Throws Error if offset + size exceeds the blob size, or on other errors.
    void read(void* buffer, int size, int offset);

    /// Write @p size bytes from @p data into the blob starting at @p offset.
    /// Throws Error if offset + size exceeds the blob size, or on other errors.
    /// Cannot resize the blob — use UPDATE to change the blob size first.
    void write(const void* data, int size, int offset);

    /// Return the number of bytes in the blob.
    int size() const noexcept;

    /// Reopen the blob handle on a different row of the same table/column.
    /// @p rowid   The new rowid to point at.
    void reopen(int64_t rowid);

    // === Convenience ===

    /// Read the entire blob contents into a byte vector.
    std::vector<uint8_t> read_all();

    /// Read the entire blob contents as a string.
    std::string read_all_text();

    /// Write a byte vector at offset 0.  The existing blob must be at least
    /// @p data.size() bytes long.
    void write_all(const std::vector<uint8_t>& data);

    /// Write raw bytes at offset 0.  The existing blob must be at least
    /// @p size bytes long.
    void write_all(const void* data, int size);

    private:
    sqlite3_blob* blob_ = nullptr;
    Database* db_ = nullptr;
};

} // namespace sqlitex
