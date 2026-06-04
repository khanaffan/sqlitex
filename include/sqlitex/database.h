#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"
#include "sqlitex/value.h"
#include "sqlitex/statement.h"
#include "sqlitex/function.h"

#include <sqlite3.h>

#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Statement;
class Session;
class VirtualTable;
class ModuleRegistration;

// ============================================================================
// Open flags
// ============================================================================

enum class OpenFlag : int {
    ReadOnly = SQLITE_OPEN_READONLY,
    ReadWrite = SQLITE_OPEN_READWRITE,
    Create = SQLITE_OPEN_CREATE,
    Uri = SQLITE_OPEN_URI,
    Memory = SQLITE_OPEN_MEMORY,
    NoMutex = SQLITE_OPEN_NOMUTEX,
    FullMutex = SQLITE_OPEN_FULLMUTEX,
    SharedCache = SQLITE_OPEN_SHAREDCACHE,
    PrivateCache = SQLITE_OPEN_PRIVATECACHE,
};

/// Bitmask of OpenFlag values.  Supports operator| between flags.
using OpenFlags = int;

inline OpenFlags operator|(OpenFlag a, OpenFlag b) noexcept {
    return static_cast<int>(a) | static_cast<int>(b);
}
inline OpenFlags operator|(OpenFlags a, OpenFlag b) noexcept {
    return a | static_cast<int>(b);
}
inline OpenFlags operator|(OpenFlag a, OpenFlags b) noexcept {
    return static_cast<int>(a) | b;
}

// ============================================================================
// Database — RAII wrapper for sqlite3*
// ============================================================================

class SQLITEX_EXPORT Database {
    public:
    Database() noexcept = default;

    /// Open a database file.
    explicit Database(std::string_view path,
                      OpenFlags flags = static_cast<int>(OpenFlag::ReadWrite) |
                                        static_cast<int>(OpenFlag::Create),
                      std::string_view vfs = {});

    ~Database() = default;

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

    explicit operator bool() const noexcept {
        return db_holder_ != nullptr;
    }

    sqlite3* handle() noexcept {
        return db_holder_.get();
    }
    const sqlite3* handle() const noexcept {
        return db_holder_.get();
    }

    // === Query Execution ===

    /// Prepare a SQL statement.
    [[nodiscard]] Statement prepare(std::string_view sql);

    // === Custom SQL Functions ===

    /// Register a scalar SQL function.
    /// \param name   Function name as used in SQL.
    /// \param n_args Number of arguments (-1 = variadic).
    /// \param fn     The callback, invoked once per row.
    /// \param flags  SQLITE_UTF8 (default), SQLITE_DETERMINISTIC, etc.
    void create_function(std::string_view name, int n_args, ScalarFunction fn,
                         int flags = SQLITE_UTF8);

    /// Register an aggregate SQL function.
    /// \param name    Function name.
    /// \param n_args  Number of arguments (-1 = variadic).
    /// \param factory Produces a new AggregateFunction instance per group.
    /// \param flags   SQLITE_UTF8, etc.
    void create_aggregate(
        std::string_view name, int n_args,
        std::function<std::unique_ptr<AggregateFunction>()> factory,
        int flags = SQLITE_UTF8);

    /// Register a window SQL function.
    /// \param name    Function name.
    /// \param n_args  Number of arguments (-1 = variadic).
    /// \param factory Produces a new WindowFunction instance per partition.
    /// \param flags   SQLITE_UTF8, etc.
    void create_window_function(
        std::string_view name, int n_args,
        std::function<std::unique_ptr<WindowFunction>()> factory,
        int flags = SQLITE_UTF8);

    /// Convenience: register an aggregate with explicit state type T.
    /// The step/final lambdas receive a reference to T for per-group state.
    template <typename T>
    void create_aggregate(
        std::string_view name, int n_args,
        std::function<void(FunctionContext&, T&, const std::vector<Value>&)>
            step,
        std::function<void(FunctionContext&, T&)> final,
        int flags = SQLITE_UTF8);

    // === Virtual Table Modules ===

    /// Register a virtual table module.
    /// \param name    Module name (used in CREATE VIRTUAL TABLE ... USING
    /// name).
    /// \param factory Produces a new VirtualTable instance per connection.
    /// \param flags   e.g. SQLITE_VTAB_INNOCUOUS.
    /// \return RAII handle; module is unregistered when destroyed.
    ModuleRegistration create_module(
        std::string_view name,
        std::function<std::unique_ptr<VirtualTable>()> factory, int flags = 0);

    // === Query Execution ===

    /// Execute a single SQL statement (no result rows).
    void execute(std::string_view sql);

    /// Execute SQL with a per-row callback.
    /// The callback receives column count, column values, and column names.
    /// Return false to stop iteration early.
    using RowCallback =
        std::function<bool(int ncols, char** values, char** names)>;

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

    /// RAII transaction guard.  Commits on normal scope exit; rolls back
    /// if an exception is in flight or rollback() was called explicitly.
    class Transaction {
        public:
        explicit Transaction(Database& db)
            : db_(&db), exceptions_(std::uncaught_exceptions()) {
            db_->execute("BEGIN");
        }
        ~Transaction() {
            if (db_ != nullptr) {
                const char* sql = (std::uncaught_exceptions() > exceptions_)
                                      ? "ROLLBACK"
                                      : "COMMIT";
                try {
                    db_->execute(sql);
                } catch (...) {
                    // Intentionally empty: commit/rollback in destructor
                    // must not throw.
                }
            }
        }

        Transaction(Transaction&& other) noexcept
            : db_(std::exchange(other.db_, nullptr)),
              exceptions_(other.exceptions_) {
        }
        Transaction& operator=(Transaction&& other) noexcept {
            if (this != &other) {
                if (db_ != nullptr) {
                    const char* sql = (std::uncaught_exceptions() > exceptions_)
                                          ? "ROLLBACK"
                                          : "COMMIT";
                    try {
                        db_->execute(sql);
                    } catch (...) {
                        // Intentionally empty: must not throw in destructor
                    }
                }
                db_ = std::exchange(other.db_, nullptr);
                exceptions_ = other.exceptions_;
            }
            return *this;
        }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        void commit() {
            if (db_ != nullptr) {
                db_->execute("COMMIT");
                db_ = nullptr;
            }
        }
        void rollback() {
            if (db_ != nullptr) {
                db_->execute("ROLLBACK");
                db_ = nullptr;
            }
        }

        private:
        Database* db_ = nullptr;
        int exceptions_ = 0;
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
        void operator()(sqlite3* db) noexcept {
            sqlite3_close_v2(db);
        }
    };
    std::unique_ptr<sqlite3, Deleter> db_holder_{};
};

// ============================================================================
// Template implementations
// ============================================================================

template <typename T>
std::optional<T> Database::query_value(std::string_view sql) {
    Statement stmt = prepare(sql);
    if (!stmt.step())
        return std::nullopt;
    if (stmt.column_count() == 0)
        return std::nullopt;
    if constexpr (std::is_same_v<T, int64_t>) {
        return stmt.column_int64(0);
    } else if constexpr (std::is_same_v<T, double>) {
        return stmt.column_double(0);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        // std::string_view would dangle: the Statement is destroyed on return.
        // Use query_value<std::string> instead.
        static_assert(!std::is_same_v<T, std::string_view>,
                      "query_value<std::string_view> is unsafe: the returned "
                      "view would dangle because the Statement is destroyed "
                      "when this function returns. "
                      "Use query_value<std::string> instead.");
    } else if constexpr (std::is_same_v<T, std::string>) {
        return std::string(stmt.column_text(0));
    } else {
        static_assert(sizeof(T) == 0, "Unsupported query_value<T> type");
    }
}

template <typename T>
void Database::create_aggregate(
    std::string_view name, int n_args,
    std::function<void(FunctionContext&, T&, const std::vector<Value>&)> step,
    std::function<void(FunctionContext&, T&)> final, int flags) {
    // Wrap the state type T in an AggregateFunction subclass.
    struct TypedAggregate : public AggregateFunction {
        T state_{};
        std::function<void(FunctionContext&, T&, const std::vector<Value>&)>
            step_fn{};
        std::function<void(FunctionContext&, T&)> final_fn{};

        void step(FunctionContext& ctx,
                  const std::vector<Value>& args) override {
            step_fn(ctx, state_, args);
        }
        void final(FunctionContext& ctx) override {
            final_fn(ctx, state_);
        }
    };

    auto factory =
        [step = std::move(step),
         final = std::move(final)]() -> std::unique_ptr<AggregateFunction> {
        auto agg = std::make_unique<TypedAggregate>();
        agg->step_fn = step;
        agg->final_fn = final;
        return agg;
    };

    create_aggregate(name, n_args, std::move(factory), flags);
}

} // namespace sqlitex
