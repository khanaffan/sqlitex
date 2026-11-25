#include "sqlitex/blobio.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include "policy_internal.h"

#include <sqlite3.h>

#include <cstring>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

BlobIO::BlobIO(Database& db, std::string_view db_name, std::string_view table,
               std::string_view column, int64_t rowid, BlobOpenMode mode)
    : db_(&db) {
    std::string db_str(db_name);
    std::string tbl_str(table);
    std::string col_str(column);
    bool const readwrite = (mode == BlobOpenMode::ReadWrite);

    detail::check_blob_access(db, db_name, table, column, readwrite);

    sqlite3_blob* raw = nullptr;
    int rc = sqlite3_blob_open(db_->handle(), db_str.c_str(), tbl_str.c_str(),
                               col_str.c_str(), rowid, readwrite ? 1 : 0, &raw);
    throw_on_error(rc, db_->handle());
    blob_ = raw;
}

BlobIO::~BlobIO() {
    if (blob_ != nullptr) {
        sqlite3_blob_close(blob_);
    }
}

BlobIO::BlobIO(BlobIO&& other) noexcept
    : blob_(std::exchange(other.blob_, nullptr)),
      db_(std::exchange(other.db_, nullptr)) {
}

BlobIO& BlobIO::operator=(BlobIO&& other) noexcept {
    if (this != &other) {
        if (blob_ != nullptr) {
            sqlite3_blob_close(blob_);
        }
        blob_ = std::exchange(other.blob_, nullptr);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

// ============================================================================
// I/O
// ============================================================================

void BlobIO::read(std::span<std::byte> buffer, int offset) {
    int const rc = sqlite3_blob_read(blob_, buffer.data(),
                                     static_cast<int>(buffer.size()), offset);
    throw_on_error(rc, db_->handle());
}

void BlobIO::write(std::span<const std::byte> data, int offset) {
    int const rc = sqlite3_blob_write(blob_, data.data(),
                                      static_cast<int>(data.size()), offset);
    throw_on_error(rc, db_->handle());
}

void BlobIO::write(std::string_view text, int offset) {
    write(std::as_bytes(std::span(text.data(), text.size())), offset);
}

int BlobIO::size() const noexcept {
    return sqlite3_blob_bytes(blob_);
}

void BlobIO::reopen(int64_t rowid) {
    int rc = sqlite3_blob_reopen(blob_, rowid);
    throw_on_error(rc, db_->handle());
}

// ============================================================================
// Convenience
// ============================================================================

std::vector<std::byte> BlobIO::read_all() {
    int const n = size();
    std::vector<std::byte> result(static_cast<std::size_t>(n));
    if (n > 0) {
        read(std::span<std::byte>(result), 0);
    }
    return result;
}

std::string BlobIO::read_all_text() {
    int const n = size();
    std::string result(static_cast<std::size_t>(n), '\0');
    if (n > 0) {
        read(std::as_writable_bytes(std::span(result.data(), result.size())),
             0);
    }
    return result;
}

void BlobIO::write_all(std::span<const std::byte> data) {
    write(data, 0);
}

void BlobIO::write_all(std::string_view text) {
    write(text, 0);
}

} // namespace sqlitex
