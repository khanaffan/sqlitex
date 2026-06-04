#pragma once

#include "sqlitex_export.h"

#include "sqlitex/value.h"

#include <sqlite3.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

// ============================================================================
// IndexInfo — wraps sqlite3_index_info for xBestIndex
// ============================================================================

/// Wrapper for sqlite3_index_info that provides type-safe access to
/// query planner inputs and outputs during xBestIndex.
class SQLITEX_EXPORT IndexInfo {
    public:
    /// Constraint operators.
    enum Op : unsigned char {
        Eq = SQLITE_INDEX_CONSTRAINT_EQ,
        Gt = SQLITE_INDEX_CONSTRAINT_GT,
        Le = SQLITE_INDEX_CONSTRAINT_LE,
        Lt = SQLITE_INDEX_CONSTRAINT_LT,
        Ge = SQLITE_INDEX_CONSTRAINT_GE,
        Match = SQLITE_INDEX_CONSTRAINT_MATCH,
        Like = SQLITE_INDEX_CONSTRAINT_LIKE,
        Glob = SQLITE_INDEX_CONSTRAINT_GLOB,
        Regexp = SQLITE_INDEX_CONSTRAINT_REGEXP,
        Ne = SQLITE_INDEX_CONSTRAINT_NE,
        IsNotNull = SQLITE_INDEX_CONSTRAINT_ISNOTNULL,
        IsNull = SQLITE_INDEX_CONSTRAINT_ISNULL,
        Is = SQLITE_INDEX_CONSTRAINT_IS,
        IsNot = SQLITE_INDEX_CONSTRAINT_ISNOT,
        Limit = SQLITE_INDEX_CONSTRAINT_LIMIT,
        Offset = SQLITE_INDEX_CONSTRAINT_OFFSET,
        Function = SQLITE_INDEX_CONSTRAINT_FUNCTION,
    };

    struct Constraint {
        int column;  ///< Column index (0-based), -1 = rowid
        Op op;       ///< Constraint operator
        bool usable; ///< True if this constraint is usable
    };

    struct OrderBy {
        int column; ///< Column index (0-based)
        bool desc;  ///< True for DESC order
    };

    explicit IndexInfo(sqlite3_index_info* info) noexcept : info_(info) {
    }

    // --- Input accessors ---

    int num_constraints() const noexcept {
        return info_->nConstraint;
    }
    Constraint constraint(int i) const noexcept {
        auto& c = info_->aConstraint[i];
        return {c.iColumn, static_cast<Op>(c.op), c.usable != 0};
    }

    int num_order_by() const noexcept {
        return info_->nOrderBy;
    }
    OrderBy order_by(int i) const noexcept {
        auto& o = info_->aOrderBy[i];
        return {o.iColumn, o.desc != 0};
    }

    bool col_used(int column) const noexcept {
        return (info_->colUsed & (static_cast<sqlite3_uint64>(1) << column)) !=
               0;
    }

    // --- Output setters ---

    /// Tell SQLite that a constraint does not need to be checked again.
    void set_omit(int constraint_index, bool omit = true) {
        info_->aConstraintUsage[constraint_index].omit = omit ? 1 : 0;
    }

    /// Assign an argv index to a constraint so its value is passed to filter().
    void set_argv_index(int constraint_index, int argv_index) {
        info_->aConstraintUsage[constraint_index].argvIndex = argv_index + 1;
    }

    /// Set the index number passed to filter().
    void set_idx_num(int num) noexcept {
        info_->idxNum = num;
    }

    /// Set the index string passed to filter() (copied).
    void set_idx_str(const char* str);
    void set_idx_str(std::string_view str);

    /// Tell SQLite that the output is already ordered correctly.
    void set_order_by_consumed(bool consumed) {
        info_->orderByConsumed = consumed ? 1 : 0;
    }

    /// Set the estimated cost of this query plan.
    void set_estimated_cost(double cost) {
        info_->estimatedCost = cost;
    }

    /// Set the estimated number of rows returned.
    void set_estimated_rows(int64_t rows) {
        info_->estimatedRows = rows;
    }

    /// Set the index flags (e.g. SQLITE_INDEX_SCAN_UNIQUE).
    void set_idx_flags(int flags) noexcept {
        info_->idxFlags = flags;
    }

    // --- Raw access ---
    sqlite3_index_info* handle() noexcept {
        return info_;
    }
    const sqlite3_index_info* handle() const noexcept {
        return info_;
    }

    private:
    sqlite3_index_info* info_;
    std::string idx_str_storage_{}; // owned copy
};

// ============================================================================
// Cursor — per-query iteration state
// ============================================================================

/// Represents a single scan of a virtual table.
/// One instance is created per query (via VirtualTable::open()).
/// The SQLite engine drives the scan by calling filter(), then repeatedly
/// calling next() + eof() + column() + rowid().
class SQLITEX_EXPORT Cursor {
    public:
    virtual ~Cursor() = default;

    /// Begin a scan.
    /// \param idx_num  Index number set in best_index() via set_idx_num().
    /// \param idx_str  Index string set in best_index() via set_idx_str().
    /// \param argv     RHS values of constraints assigned via set_argv_index().
    virtual int filter(int idx_num, const char* idx_str,
                       const std::vector<Value>& argv) = 0;

    /// Advance to the next row.  Return SQLITE_OK on success.
    virtual int next() = 0;

    /// Return true when there are no more rows.
    virtual bool eof() = 0;

    /// Return the value of column `i` for the current row.
    virtual Value column(int i) = 0;

    /// Return the rowid of the current row.
    virtual int64_t rowid() = 0;
};

// ============================================================================
// VirtualTable — the module per-connection instance
// ============================================================================

/// Base class for implementing a virtual table module.
///
/// Subclass this and register a factory with Database::create_module().
///
/// **Read-only vtables**: override create(), best_index(), and open().
/// **Read-write vtables**: additionally override update() and optionally
///   begin_txn(), sync(), commit_txn(), rollback_txn().
///
/// One instance is created per connection (via the factory) when the table
/// is attached.  The C++ instance is destroyed when the table is detached
/// or the database connection closes.
class SQLITEX_EXPORT VirtualTable {
    public:
    VirtualTable() = default;
    virtual ~VirtualTable() = default;

    // --- Schema / lifecycle ---

    /// Called during CREATE VIRTUAL TABLE.
    /// Must call sqlite3_declare_vtab(db, schema) to define the schema.
    /// \param db     Database handle (for sqlite3_declare_vtab).
    /// \param argc   Number of arguments (always >= 3).
    /// \param argv   argv[0]=module name, argv[1]=db name, argv[2]=table name,
    ///               argv[3..]=arguments from the CREATE VIRTUAL TABLE
    ///               statement.
    /// \return SQLITE_OK on success, or an error code.
    virtual int create(sqlite3* db, int argc, const char* const* argv) = 0;

    /// Called when an existing table is re-connected (e.g., new connection).
    /// Default calls create().  Override if connection differs from creation.
    virtual int connect(sqlite3* db, int argc, const char* const* argv) {
        return create(db, argc, argv);
    }

    /// Called when the table is dropped (DROP TABLE / DROP VIRTUAL TABLE).
    /// Default returns SQLITE_OK.
    virtual int destroy() {
        return SQLITE_OK;
    }

    /// Called when the connection closes or the table is DETACHed.
    /// Default returns SQLITE_OK.
    virtual int disconnect() {
        return SQLITE_OK;
    }

    // --- Query planning ---

    /// Analyze a query and choose an execution plan.
    /// Fill in output fields of `info`:
    ///   - set_idx_num() / set_idx_str() to identify the plan to filter()
    ///   - set_estimated_cost() / set_estimated_rows() for the cost estimate
    ///   - set_argv_index() for each constraint used as an argument
    ///   - set_omit() for constraints that don't need re-checking
    virtual int best_index(IndexInfo info) = 0;

    // --- Cursor management ---

    /// Create a new cursor for scanning this table.
    /// Set `cursor` to a new Cursor instance and return SQLITE_OK.
    /// The cursor will be destroyed by SQLite when the scan finishes.
    virtual int open(std::unique_ptr<Cursor>& cursor) = 0;

    // --- Mutating operations (read-write vtables only) ---

    /// Insert, update, or delete a row.
    /// \param argv[0] = rowid to delete (DELETE), NULL (INSERT), or old rowid
    /// (UPDATE)
    /// \param argv[1..] = new column values (INSERT/UPDATE)
    /// \param p_rowid  On INSERT, set to the new rowid.
    /// Default returns SQLITE_READONLY (making the table read-only).
    virtual int update(int argc, sqlite3_value** argv, int64_t* p_rowid) {
        (void)argc;
        (void)argv;
        (void)p_rowid;
        return SQLITE_READONLY;
    }

    /// Begin a transaction on this virtual table.
    virtual int begin_txn() {
        return SQLITE_OK;
    }

    /// Sync phase of a transaction.
    virtual int sync() {
        return SQLITE_OK;
    }

    /// Commit a transaction.
    virtual int commit_txn() {
        return SQLITE_OK;
    }

    /// Rollback a transaction.
    virtual int rollback_txn() {
        return SQLITE_OK;
    }

    /// Rename a virtual table.
    virtual int rename(const char* /*new_name*/) {
        return SQLITE_OK;
    }

    /// Optional: savepoint support (requires iVersion >= 2).
    virtual int savepoint(int /*op*/, int /*savepoint*/) {
        return SQLITE_OK;
    }

    /// Optional: override function dispatch for this table.
    /// \return 0 if not found, non-zero if function was set via *px_func.
    virtual int find_function(int /*n_arg*/, const char* /*name*/,
                              void (** /*px_func*/)(sqlite3_context*, int,
                                                    sqlite3_value**),
                              void** /*pp_arg*/) {
        return 0;
    }
};

// ============================================================================
// ModuleRegistration — RAII handle for a registered module
// ============================================================================

/// Returned by Database::create_module().  Keeps the module registered
/// until destroyed.  When the last copy is destroyed, the module is
/// unregistered from the database.
class SQLITEX_EXPORT ModuleRegistration {
    public:
    ModuleRegistration() = default;
    ~ModuleRegistration();

    ModuleRegistration(ModuleRegistration&& other) noexcept;
    ModuleRegistration& operator=(ModuleRegistration&& other) noexcept;
    ModuleRegistration(const ModuleRegistration&) = delete;
    ModuleRegistration& operator=(const ModuleRegistration&) = delete;

    explicit operator bool() const noexcept {
        return db_ != nullptr;
    }

    private:
    friend class Database;
    friend ModuleRegistration create_module_impl(
        sqlite3* db, std::string_view name,
        std::function<std::unique_ptr<VirtualTable>()> factory, int flags);

    ModuleRegistration(sqlite3* db, std::string name,
                       std::shared_ptr<void> module_ctx);

    sqlite3* db_ = nullptr;
    std::string name_{};
    std::shared_ptr<void> module_ctx_{};
};

} // namespace sqlitex
