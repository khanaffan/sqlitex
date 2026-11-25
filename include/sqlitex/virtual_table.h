#pragma once

#include "sqlitex_export.h"

#include "sqlitex/value.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <functional>
#include <memory>
#include <optional>
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

    int num_constraints() const noexcept;
    Constraint constraint(int i) const noexcept;

    int num_order_by() const noexcept;
    OrderBy order_by(int i) const noexcept;

    bool col_used(int column) const noexcept;

    // --- Output setters ---

    /// Tell SQLite that a constraint does not need to be checked again.
    void set_omit(int constraint_index, bool omit = true);

    /// Assign an argv index to a constraint so its value is passed to filter().
    void set_argv_index(int constraint_index, int argv_index);

    /// Set the index number passed to filter().
    void set_idx_num(int num) noexcept;

    /// Set the index string passed to filter() (copied).
    void set_idx_str(const char* str);
    void set_idx_str(std::string_view str);

    /// Tell SQLite that the output is already ordered correctly.
    void set_order_by_consumed(bool consumed);

    /// Set the estimated cost of this query plan.
    void set_estimated_cost(double cost);

    /// Set the estimated number of rows returned.
    void set_estimated_rows(int64_t rows);

    /// Set the index flags (e.g. SQLITE_INDEX_SCAN_UNIQUE).
    void set_idx_flags(int flags) noexcept;

    /// Query the collation name used for constraint `constraint_index`
    /// (sqlite3_vtab_collation).
    std::string_view collation(int constraint_index) const noexcept;

    /// Query the DISTINCT mode for this query (sqlite3_vtab_distinct):
    /// 0 = normal, 1 = GROUP BY/DISTINCT, 2 = DISTINCT, 3 = DISTINCT + ORDER
    /// BY.
    int distinct() const noexcept;

    /// Query or opt into handling an IN(...) constraint in a single xFilter
    /// call (sqlite3_vtab_in). Pass handle_mode = -1 to query, 0 to disable,
    /// 1 to enable.
    bool set_in(int constraint_index, int handle_mode = 1) noexcept;

    /// Query the RHS constant value of a constraint if known at planning time
    /// (sqlite3_vtab_rhs_value). Returns std::nullopt if unavailable.
    std::optional<Value> rhs_value(int constraint_index) const;

    // --- Raw access ---
    sqlite3_index_info* handle() noexcept {
        return info_;
    }
    const sqlite3_index_info* handle() const noexcept {
        return info_;
    }

    private:
    sqlite3_index_info* info_;
};

// ============================================================================
// VtabInIterator — iterates elements of an IN(...) constraint in xFilter
// ============================================================================

class SQLITEX_EXPORT VtabInIterator {
    public:
    explicit VtabInIterator(sqlite3_value* in_list) noexcept;
    explicit VtabInIterator(const ValueRef& in_list) noexcept
        : VtabInIterator(in_list.handle()) {
    }

    /// Advance to the next value in the IN list.
    /// Returns true if a value is available via current() / current_value().
    bool next();

    ValueRef current() const noexcept {
        return ValueRef(current_);
    }

    Value current_value() const {
        return current().to_value();
    }

    /// Convenience: collect all elements from an IN list into a vector.
    static std::vector<Value> collect(sqlite3_value* in_list);
    static std::vector<Value> collect(const ValueRef& in_list) {
        return collect(in_list.handle());
    }

    private:
    sqlite3_value* in_list_ = nullptr;
    sqlite3_value* current_ = nullptr;
    bool started_ = false;
    bool done_ = false;
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

    /// Begin a scan with raw ValueRef arguments (needed for VtabInIterator
    /// when SQLITE_VTAB_IN is enabled). Default converts args to Value and
    /// delegates to filter().
    virtual int filter_raw(int idx_num, const char* idx_str,
                           const std::vector<ValueRef>& argv);

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

enum class VtabConfig : int {
    ConstraintSupport = SQLITE_VTAB_CONSTRAINT_SUPPORT,
    Innocuous = SQLITE_VTAB_INNOCUOUS,
    DirectOnly = SQLITE_VTAB_DIRECTONLY,
    UsesAllSchemas = SQLITE_VTAB_USES_ALL_SCHEMAS,
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

    // --- Helper wrappers for SQLite vtab C APIs ---

    /// Declare the virtual table schema inside create() / connect()
    /// (sqlite3_declare_vtab).
    static int declare_vtab(sqlite3* db, std::string_view schema);

    /// Configure virtual table attributes inside create() / connect()
    /// (sqlite3_vtab_config). `val` is used for ConstraintSupport.
    static int vtab_config(sqlite3* db, VtabConfig op, int val = 1);

    /// Return the current ON CONFLICT resolution mode inside update()
    /// (sqlite3_vtab_on_conflict).
    static int vtab_on_conflict(sqlite3* db) noexcept;

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
    ///
    /// The argv vector mirrors the raw SQLite xUpdate arguments:
    /// - argv[0] : old rowid to delete (DELETE/UPDATE), or NULL Value for
    /// INSERT
    /// - argv[1] : new rowid (INSERT/UPDATE), or NULL Value
    /// - argv[2..] : new column values (INSERT/UPDATE)
    ///
    /// \param argc    Number of elements in argv (same as argc from SQLite).
    /// \param argv    Typed Value wrappers for all xUpdate arguments.
    /// \param p_rowid On INSERT, set to the new rowid.
    ///
    /// Default returns SQLITE_READONLY (making the table read-only).
    virtual int update(int argc, const std::vector<Value>& argv,
                       int64_t* p_rowid) {
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
