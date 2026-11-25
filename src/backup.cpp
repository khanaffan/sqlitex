#include "sqlitex/backup.h"
#include "sqlitex/database.h"

#include <sqlite3.h>

#include <string>
#include <thread>
#include <utility>

namespace sqlitex {

Backup::Backup(Database& dest, Database& source, std::string_view dest_name,
               std::string_view source_name)
    : dest_(&dest) {
    std::string dest_str(dest_name);
    std::string src_str(source_name);
    backup_ = sqlite3_backup_init(dest.handle(), dest_str.c_str(),
                                  source.handle(), src_str.c_str());
    if (backup_ == nullptr) {
        throw_sqlite_error(sqlite3_errcode(dest.handle()), dest.handle());
    }
}

Backup::~Backup() {
    if (backup_ != nullptr) {
        sqlite3_backup_finish(backup_);
    }
}

Backup::Backup(Backup&& other) noexcept
    : backup_(std::exchange(other.backup_, nullptr)),
      dest_(std::exchange(other.dest_, nullptr)) {
}

Backup& Backup::operator=(Backup&& other) noexcept {
    if (this != &other) {
        if (backup_ != nullptr) {
            sqlite3_backup_finish(backup_);
        }
        backup_ = std::exchange(other.backup_, nullptr);
        dest_ = std::exchange(other.dest_, nullptr);
    }
    return *this;
}

int Backup::step_raw(int pages) {
    if (backup_ == nullptr) {
        throw MisuseError(ErrorCode::Misuse,
                          "Backup::step() called on a finished or null Backup");
    }
    int rc = sqlite3_backup_step(backup_, pages);
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_BUSY ||
        rc == SQLITE_LOCKED) {
        return rc;
    }
    throw_sqlite_error(rc, dest_ ? dest_->handle() : nullptr);
}

bool Backup::step(int pages) {
    int rc = step_raw(pages);
    if (rc == SQLITE_DONE) {
        return true;
    }
    if (rc == SQLITE_OK) {
        return false;
    }
    throw_sqlite_error(rc, dest_ ? dest_->handle() : nullptr);
}

void Backup::finish() {
    if (backup_ == nullptr) {
        return;
    }
    sqlite3_backup* b = std::exchange(backup_, nullptr);
    int rc = sqlite3_backup_finish(b);
    if (rc != SQLITE_OK) {
        throw_sqlite_error(rc, dest_ ? dest_->handle() : nullptr);
    }
}

int Backup::remaining() const noexcept {
    return backup_ ? sqlite3_backup_remaining(backup_) : 0;
}

int Backup::pagecount() const noexcept {
    return backup_ ? sqlite3_backup_pagecount(backup_) : 0;
}

void Backup::run(int pages_per_step,
                 std::function<void(int remaining, int pagecount)> progress,
                 std::chrono::milliseconds sleep_on_busy) {
    int rc = SQLITE_OK;
    do {
        rc = step_raw(pages_per_step);
        if (progress) {
            progress(remaining(), pagecount());
        }
        if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
            if (sleep_on_busy.count() > 0) {
                std::this_thread::sleep_for(sleep_on_busy);
            }
        }
    } while (rc == SQLITE_OK || rc == SQLITE_BUSY || rc == SQLITE_LOCKED);

    finish();
}

} // namespace sqlitex
