#pragma once

#include "sqlitex_export.h"

#include "sqlitex/flags.h"
#include "sqlitex/value.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

// ============================================================================
// Virtual table strong enum types and bitmasks
// ============================================================================

enum class IndexScanFlag : int {
    Unique = SQLITE_INDEX_SCAN_UNIQUE,
    Hex = SQLITE_INDEX_SCAN_HEX,
};

template <>
struct EnableBitmaskOperators<IndexScanFlag> : std::true_type {};

using IndexScanFlags = Flags<IndexScanFlag>;

enum class DistinctMode : int {
    Normal = 0,
    GroupBy = 1,
    Distinct = 2,
    DistinctOrderBy = 3,
};

enum class ConflictResolution : int {
    Rollback = SQLITE_ROLLBACK,
    Ignore = SQLITE_IGNORE,
    Fail = SQLITE_FAIL,
    Abort = SQLITE_ABORT,
    Replace = SQLITE_REPLACE,
};

enum class SavepointOp : int {
    Begin = 0,
    Release = 1,
    Rollback = 2,
};

// ============================================================================
// IndexInfo — wraps sqlite3_index_info for xBestIndex
// ============================================================================

/// Wrapper for sqlite3_index_info that provides type-safe access to
/// query planner inputs and outputs during xBestIndex.
class SQLITEX_EXPORT IndexInfo {
    public:
    /// Constraint operators.
    enum class Op : unsigned char {
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

    [[nodiscard]] int num_constraints() const noexcept;
    [[nodiscard]] Constraint constraint(int i) const noexcept;

    [[nodiscard]] int num_order_by() const noexcept;
    [[nodiscard]] OrderBy order_by(int i) const noexcept;

    [[nodiscard]] bool col_used(int column) const noexcept;

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

    /// Set the index scan flags (e.g. IndexScanFlag::Unique).
    void set_idx_flags(IndexScanFlags flags) noexcept;

    /// Query the collation name used for constraint `constraint_index`
    /// (sqlite3_vtab_collation).
    [[nodiscard]] std::string_view collation(
        int constraint_index) const noexcept;

    /// Query the DISTINCT mode for this query (sqlite3_vtab_distinct).
    [[nodiscard]] DistinctMode distinct() const noexcept;

    /// Query or opt into handling an IN(...) constraint in a single xFilter
    /// call (sqlite3_vtab_in). Pass handle_mode = -1 to query, 0 to disable,
    /// 1 to enable.
    bool set_in(int constraint_index, int handle_mode = 1) noexcept;

    /// Query the RHS constant value of a constraint if known at planning time
    /// (sqlite3_vtab_rhs_value). Returns std::nullopt if unavailable.
    [[nodiscard]] std::optional<Value> rhs_value(int constraint_index) const;

    // --- Raw access ---
    [[nodiscard]] sqlite3_index_info* handle() noexcept {
        return info_;
    }
    [[nodiscard]] const sqlite3_index_info* handle() const noexcept {
        return info_;
    }

    private:
    sqlite3_index_info* info_;
};

// ============================================================================
// VtabInIterator — iterates elements of an IN(...) constraint in xFilter
// ============================================================================

class SQLITEX_EXPORT VtabInIterator
    : public std::ranges::view_interface<VtabInIterator> {
    public:
    class Iterator {
        public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = ValueRef;
        using difference_type = std::ptrdiff_t;

        Iterator() noexcept = default;
        explicit Iterator(VtabInIterator* owner) noexcept : owner_(owner) {
        }

        [[nodiscard]] ValueRef operator*() const noexcept {
            return owner_->current();
        }

        Iterator& operator++() {
            if (!owner_->next()) {
                owner_ = nullptr;
            }
            return *this;
        }

        void operator++(int) {
            ++(*this);
        }

        [[nodiscard]] bool operator==(std::default_sentinel_t) const noexcept {
            return owner_ == nullptr || owner_->done_;
        }

        private:
        VtabInIterator* owner_ = nullptr;
    };

    VtabInIterator() noexcept = default;
    explicit VtabInIterator(sqlite3_value* in_list) noexcept;
    explicit VtabInIterator(const ValueRef& in_list) noexcept
        : VtabInIterator(in_list.handle()) {
    }

    /// Advance to the next value in the IN list.
    /// Returns true if a value is available via current() / current_value().
    bool next();

    [[nodiscard]] ValueRef current() const noexcept {
        return ValueRef(current_);
    }

    [[nodiscard]] Value current_value() const {
        return current().to_value();
    }

    [[nodiscard]] Iterator begin() {
        if (!started_ && !done_) {
            if (!next()) {
                return Iterator{};
            }
        }
        return done_ ? Iterator{} : Iterator{this};
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

    /// Convenience: collect all elements from an IN list into a vector.
    [[nodiscard]] static std::vector<Value> collect(sqlite3_value* in_list);
    [[nodiscard]] static std::vector<Value> collect(const ValueRef& in_list) {
        return collect(in_list.handle());
    }

    private:
    sqlite3_value* in_list_ = nullptr;
    sqlite3_value* current_ = nullptr;
    bool started_ = false;
    bool done_ = true;
};

static_assert(std::ranges::input_range<VtabInIterator>);
static_assert(std::ranges::view<VtabInIterator>);

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
                           std::span<const ValueRef> argv);

    /// Begin a scan.
    /// \param idx_num  Index number set in best_index() via set_idx_num().
    /// \param idx_str  Index string set in best_index() via set_idx_str().
    /// \param argv     RHS values of constraints assigned via set_argv_index().
    virtual int filter(int idx_num, const char* idx_str,
                       std::span<const Value> argv) = 0;

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
    [[nodiscard]] static ConflictResolution vtab_on_conflict(
        sqlite3* db) noexcept;

    // --- Schema / lifecycle ---

    /// Called during CREATE VIRTUAL TABLE.
    /// Must call declare_vtab(db, schema) to define the schema.
    /// \param db     Database handle (for declare_vtab).
    /// \param argv   argv[0]=module name, argv[1]=db name, argv[2]=table name,
    ///               argv[3..]=arguments from CREATE VIRTUAL TABLE.
    /// \return SQLITE_OK on success, or an error code.
    virtual int create(sqlite3* db, std::span<const std::string_view> argv) = 0;

    /// Called when an existing table is re-connected (e.g., new connection).
    /// Default calls create(). Override if connection differs from creation.
    virtual int connect(sqlite3* db, std::span<const std::string_view> argv) {
        return create(db, argv);
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
    virtual int best_index(IndexInfo info) = 0;

    // --- Cursor management ---

    /// Create a new cursor for scanning this table.
    virtual int open(std::unique_ptr<Cursor>& cursor) = 0;

    // --- Mutating operations (read-write vtables only) ---

    /// Insert, update, or delete a row.
    ///
    /// - argv[0] : old rowid to delete (DELETE/UPDATE), or NULL Value for
    /// INSERT
    /// - argv[1] : new rowid (INSERT/UPDATE), or NULL Value
    /// - argv[2..] : new column values (INSERT/UPDATE)
    ///
    /// Default returns SQLITE_READONLY (making the table read-only).
    virtual int update(std::span<const Value> argv, int64_t* p_rowid) {
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
    virtual int rename(std::string_view /*new_name*/) {
        return SQLITE_OK;
    }

    /// Optional: savepoint support (requires iVersion >= 2).
    virtual int savepoint(SavepointOp /*op*/, int /*savepoint*/) {
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

/// Returned by Database::create_module(). Keeps the module registered
/// until destroyed.
class [[nodiscard]] SQLITEX_EXPORT ModuleRegistration {
    public:
    ModuleRegistration() = default;
    ~ModuleRegistration();

    ModuleRegistration(ModuleRegistration&& other) noexcept;
    ModuleRegistration& operator=(ModuleRegistration&& other) noexcept;
    ModuleRegistration(const ModuleRegistration&) = delete;
    ModuleRegistration& operator=(const ModuleRegistration&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return db_ != nullptr;
    }

    private:
    friend class Database;
    friend ModuleRegistration create_module_impl(
        sqlite3* db, std::string_view name,
        std::function<std::unique_ptr<VirtualTable>()> factory);

    ModuleRegistration(sqlite3* db, std::string name,
                       std::shared_ptr<void> module_ctx);

    sqlite3* db_ = nullptr;
    std::string name_{};
    std::shared_ptr<void> module_ctx_{};
};

} // namespace sqlitex
