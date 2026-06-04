#include "sqlitex/blobio.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <cstring>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

BlobIO::BlobIO(Database& db, std::string_view db_name, std::string_view table,
               std::string_view column, int64_t rowid, bool readwrite)
    : db_(&db) {
    std::string db_str(db_name);
    std::string tbl_str(table);
    std::string col_str(column);

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

void BlobIO::read(void* buffer, int size, int offset) {
    int const rc = sqlite3_blob_read(blob_, buffer, size, offset);
    throw_on_error(rc, db_->handle());
}

void BlobIO::write(const void* data, int size, int offset) {
    int const rc = sqlite3_blob_write(blob_, data, size, offset);
    throw_on_error(rc, db_->handle());
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

std::vector<uint8_t> BlobIO::read_all() {
    int const n = size();
    std::vector<uint8_t> result(static_cast<std::size_t>(n));
    if (n > 0) {
        read(result.data(), n, 0);
    }
    return result;
}

std::string BlobIO::read_all_text() {
    int const n = size();
    std::string result(static_cast<std::size_t>(n), '\0');
    if (n > 0) {
        read(result.data(), n, 0);
    }
    return result;
}

void BlobIO::write_all(const std::vector<uint8_t>& data) {
    write_all(data.data(), static_cast<int>(data.size()));
}

void BlobIO::write_all(const void* data, int size) {
    write(data, size, 0);
}

} // namespace sqlitex
