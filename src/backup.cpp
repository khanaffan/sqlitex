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

Expected<BackupStepResult> Backup::try_step(int pages) {
    if (backup_ == nullptr) {
        return std::unexpected(
            Error(ErrorCode::Misuse,
                  "Backup::step() called on a finished or null Backup"));
    }
    int rc = sqlite3_backup_step(backup_, pages);
    switch (rc) {
    case SQLITE_OK:
        return BackupStepResult::More;
    case SQLITE_DONE:
        return BackupStepResult::Done;
    case SQLITE_BUSY:
        return BackupStepResult::Busy;
    case SQLITE_LOCKED:
        return BackupStepResult::Locked;
    default:
        return std::unexpected(
            make_sqlite_error(rc, dest_ ? dest_->handle() : nullptr));
    }
}

BackupStepResult Backup::step_nonblocking(int pages) {
    if (backup_ == nullptr) {
        throw MisuseError(ErrorCode::Misuse,
                          "Backup::step() called on a finished or null Backup");
    }
    int rc = sqlite3_backup_step(backup_, pages);
    switch (rc) {
    case SQLITE_OK:
        return BackupStepResult::More;
    case SQLITE_DONE:
        return BackupStepResult::Done;
    case SQLITE_BUSY:
        return BackupStepResult::Busy;
    case SQLITE_LOCKED:
        return BackupStepResult::Locked;
    default:
        throw_sqlite_error(rc, dest_ ? dest_->handle() : nullptr);
    }
}

bool Backup::step(int pages) {
    auto res = step_nonblocking(pages);
    if (res == BackupStepResult::Done) {
        return true;
    }
    if (res == BackupStepResult::More) {
        return false;
    }
    throw_sqlite_error(
        res == BackupStepResult::Busy ? SQLITE_BUSY : SQLITE_LOCKED,
        dest_ ? dest_->handle() : nullptr);
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
    BackupStepResult res = BackupStepResult::More;
    do {
        res = step_nonblocking(pages_per_step);
        if (progress) {
            progress(remaining(), pagecount());
        }
        if (res == BackupStepResult::Busy || res == BackupStepResult::Locked) {
            if (sleep_on_busy.count() > 0) {
                std::this_thread::sleep_for(sleep_on_busy);
            }
        }
    } while (res != BackupStepResult::Done);

    finish();
}

} // namespace sqlitex
