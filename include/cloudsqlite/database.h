#pragma once

#include "cloudsqlite/error.h"
#include "cloudsqlite/value.h"
#include "cloudsqlite/statement.h"

#include <sqlite3.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudsqlite {

class Statement;
class Session;

// ============================================================================
// Open flags
// ============================================================================

enum class OpenFlag : int {
    ReadOnly     = SQLITE_OPEN_READONLY,
    ReadWrite    = SQLITE_OPEN_READWRITE,
    Create       = SQLITE_OPEN_CREATE,
    Uri          = SQLITE_OPEN_URI,
    Memory       = SQLITE_OPEN_MEMORY,
    NoMutex      = SQLITE_OPEN_NOMUTEX,
    FullMutex    = SQLITE_OPEN_FULLMUTEX,
    SharedCache  = SQLITE_OPEN_SHAREDCACHE,
    PrivateCache = SQLITE_OPEN_PRIVATECACHE,
};

using OpenFlags = int;  // bitmask of OpenFlag values

// ============================================================================
// Database — RAII wrapper for sqlite3*
// ============================================================================

class Database {
public:
    Database() noexcept = default;

    /// Open a database file.
    explicit Database(std::string_view path,
                      OpenFlags flags = static_cast<int>(OpenFlag::ReadWrite) |
                                        static_cast<int>(OpenFlag::Create),
                      std::string_view vfs = {});

    ~Database();

    // Move-only
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Explicitly open/re-open the database.  Closes any previous connection.
    void open(std::string_view path,
              OpenFlags flags = static_cast<int>(OpenFlag::ReadWrite) |
                                static_cast<int>(OpenFlag::Create),
              std::string_view vfs = {});

    /// Close the database connection.  Called automatically on destruction.
    void close() noexcept;

    explicit operator bool() const noexcept { return db_ != nullptr; }

    sqlite3* handle() noexcept { return db_; }
    const sqlite3* handle() const noexcept { return db_; }

    // === Query Execution ===

    /// Prepare a SQL statement.
    [[nodiscard]] Statement prepare(std::string_view sql);

    /// Execute a single SQL statement (no result rows).
    void execute(std::string_view sql);

    /// Execute SQL with a per-row callback.
    /// The callback receives column count, column values pointer, and column
    /// names pointer.  Return false to stop iteration early.
    using RowCallback = std::function<bool(int ncols, sqlite3_value** values,
                                           const char** names)>;

    void exec(std::string_view sql, RowCallback cb);

    /// Execute SQL and collect all rows as Value vectors.
    std::vector<std::vector<Value>> query(std::string_view sql);

    /// Convenience: execute SQL and return the first column of the first row.
    template <typename T = int64_t>
    std::optional<T> query_value(std::string_view sql);

    // === Transactions ===

    void begin_transaction();
    void commit();
    void rollback();

    /// RAII transaction guard.  Commits on destruction unless rollback() is
    /// called.
    class Transaction {
    public:
        explicit Transaction(Database& db) : db_(&db) { db_->execute("BEGIN"); }
        ~Transaction() { if (db_) db_->execute("ROLLBACK"); }

        Transaction(Transaction&& other) noexcept
            : db_(std::exchange(other.db_, nullptr)) {}
        Transaction& operator=(Transaction&& other) noexcept {
            if (this != &other) {
                if (db_) db_->execute("ROLLBACK");
                db_ = std::exchange(other.db_, nullptr);
            }
            return *this;
        }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        void commit() {
            if (db_) { db_->execute("COMMIT"); db_ = nullptr; }
        }
        void rollback() {
            if (db_) { db_->execute("ROLLBACK"); db_ = nullptr; }
        }

    private:
        Database* db_ = nullptr;
    };

    // === Utility ===

    int64_t last_insert_rowid() const noexcept;
    int changes() const noexcept;
    int64_t total_changes() const noexcept;

    int errcode() const noexcept;
    int extended_errcode() const noexcept;
    std::string_view errmsg() const noexcept;

    /// Interrupt a long-running query (safe to call from another thread).
    void interrupt();

    /// Set a busy timeout in milliseconds.
    void set_busy_timeout(int ms);

    /// Check if in autocommit mode.
    bool is_autocommit() const noexcept;

    /// Get the filename for an attached database.
    std::string_view db_filename(std::string_view db_name = "main") const;

private:
    struct Deleter {
        void operator()(sqlite3* db) noexcept { sqlite3_close_v2(db); }
    };
    std::unique_ptr<sqlite3, Deleter> db_holder_;
    sqlite3* db_ = nullptr;
};

// ============================================================================
// Template implementations
// ============================================================================

template <typename T>
std::optional<T> Database::query_value(std::string_view sql) {
    Statement stmt = prepare(sql);
    if (!stmt.step()) return std::nullopt;
    if (stmt.column_count() == 0) return std::nullopt;
    if constexpr (std::is_same_v<T, int64_t>) {
        return stmt.column_int64(0);
    } else if constexpr (std::is_same_v<T, double>) {
        return stmt.column_double(0);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return stmt.column_text(0);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return std::string(stmt.column_text(0));
    } else {
        static_assert(sizeof(T) == 0, "Unsupported query_value<T> type");
    }
}

} // namespace cloudsqlite
