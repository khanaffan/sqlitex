#include "sqlitex/snapshot.h"
#include "sqlitex/database.h"

#include <sqlite3.h>

#include <string>
#include <utility>

namespace sqlitex {

Snapshot::~Snapshot() {
    if (snapshot_ != nullptr) {
        sqlite3_snapshot_free(snapshot_);
    }
}

Snapshot::Snapshot(Snapshot&& other) noexcept
    : snapshot_(std::exchange(other.snapshot_, nullptr)) {
}

Snapshot& Snapshot::operator=(Snapshot&& other) noexcept {
    if (this != &other) {
        if (snapshot_ != nullptr) {
            sqlite3_snapshot_free(snapshot_);
        }
        snapshot_ = std::exchange(other.snapshot_, nullptr);
    }
    return *this;
}

Snapshot Snapshot::get(Database& db, std::string_view schema) {
    std::string schema_str(schema);
    sqlite3_snapshot* snap = nullptr;
    int rc = sqlite3_snapshot_get(db.handle(), schema_str.c_str(), &snap);
    throw_on_error(rc, db.handle());
    return Snapshot(snap);
}

void Snapshot::open(Database& db, std::string_view schema) const {
    if (snapshot_ == nullptr) {
        throw MisuseError(ErrorCode::Misuse,
                          "Snapshot::open() called on a null Snapshot");
    }
    std::string schema_str(schema);
    int rc = sqlite3_snapshot_open(db.handle(), schema_str.c_str(), snapshot_);
    throw_on_error(rc, db.handle());
}

void Snapshot::recover(Database& db, std::string_view schema) {
    std::string schema_str(schema);
    int rc = sqlite3_snapshot_recover(db.handle(), schema_str.c_str());
    throw_on_error(rc, db.handle());
}

int Snapshot::compare(const Snapshot& a, const Snapshot& b) noexcept {
    if (a.snapshot_ == b.snapshot_) {
        return 0;
    }
    if (a.snapshot_ == nullptr) {
        return -1;
    }
    if (b.snapshot_ == nullptr) {
        return 1;
    }
    return sqlite3_snapshot_cmp(a.snapshot_, b.snapshot_);
}

} // namespace sqlitex
