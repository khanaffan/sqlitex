#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"
#include "sqlitex/function.h"
#include "sqlitex/statement.h"
#include "sqlitex/statement_cache.h"
#include "sqlitex/value.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sqlitex {

class Statement;
class Session;
class VirtualTable;
class ModuleRegistration;
class ScopedPolicy;
namespace detail {
struct PolicyAccess;
} // namespace detail

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
    NoFollow = SQLITE_OPEN_NOFOLLOW,
    ExResCode = SQLITE_OPEN_EXRESCODE,
};

template <>
struct EnableBitmaskOperators<OpenFlag> : std::true_type {};

using OpenFlags = Flags<OpenFlag>;

// ============================================================================
// Transaction modes, Checkpoint modes, Serialize/Deserialize
// ============================================================================

enum class TransactionMode {
    Deferred,
    Immediate,
    Exclusive,
};

enum class TxnState : int {
    None = SQLITE_TXN_NONE,
    Read = SQLITE_TXN_READ,
    Write = SQLITE_TXN_WRITE,
};

enum class DbReadOnlyState : int {
    NoSuchSchema = -1,
    ReadWrite = 0,
    ReadOnly = 1,
};

enum class CheckpointMode : int {
    Passive = SQLITE_CHECKPOINT_PASSIVE,
    Full = SQLITE_CHECKPOINT_FULL,
    Restart = SQLITE_CHECKPOINT_RESTART,
    Truncate = SQLITE_CHECKPOINT_TRUNCATE,
    Noop = SQLITE_CHECKPOINT_NOOP,
};

struct CheckpointResult {
    int log_frames = 0;
    int checkpointed_frames = 0;
};

enum class SerializeFlag : unsigned int {
    None = 0,
    NoCopy = SQLITE_SERIALIZE_NOCOPY,
};

template <>
struct EnableBitmaskOperators<SerializeFlag> : std::true_type {};

using SerializeFlags = Flags<SerializeFlag>;

enum class DeserializeFlag : unsigned int {
    None = 0,
    FreeOnClose = SQLITE_DESERIALIZE_FREEONCLOSE,
    Resizeable = SQLITE_DESERIALIZE_RESIZEABLE,
    ReadOnly = SQLITE_DESERIALIZE_READONLY,
};

template <>
struct EnableBitmaskOperators<DeserializeFlag> : std::true_type {};

using DeserializeFlags = Flags<DeserializeFlag>;

enum class TraceFlag : unsigned int {
    None = 0,
    Stmt = SQLITE_TRACE_STMT,
    Profile = SQLITE_TRACE_PROFILE,
    Row = SQLITE_TRACE_ROW,
    Close = SQLITE_TRACE_CLOSE,
};

template <>
struct EnableBitmaskOperators<TraceFlag> : std::true_type {};

using TraceFlags = Flags<TraceFlag>;

enum class DbConfig : int {
    EnableFKey = SQLITE_DBCONFIG_ENABLE_FKEY,
    EnableTrigger = SQLITE_DBCONFIG_ENABLE_TRIGGER,
    EnableFts3Tokenizer = SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER,
    EnableLoadExtension = SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION,
    NoCkptOnClose = SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE,
    EnableQpsg = SQLITE_DBCONFIG_ENABLE_QPSG,
    TriggerEqp = SQLITE_DBCONFIG_TRIGGER_EQP,
    ResetDatabase = SQLITE_DBCONFIG_RESET_DATABASE,
    Defensive = SQLITE_DBCONFIG_DEFENSIVE,
    WritableSchema = SQLITE_DBCONFIG_WRITABLE_SCHEMA,
    LegacyAlterTable = SQLITE_DBCONFIG_LEGACY_ALTER_TABLE,
    DqsDdl = SQLITE_DBCONFIG_DQS_DDL,
    DqsDml = SQLITE_DBCONFIG_DQS_DML,
    EnableView = SQLITE_DBCONFIG_ENABLE_VIEW,
    LegacyFileFormat = SQLITE_DBCONFIG_LEGACY_FILE_FORMAT,
    TrustedSchema = SQLITE_DBCONFIG_TRUSTED_SCHEMA,
    StmtScanStatus = SQLITE_DBCONFIG_STMT_SCANSTATUS,
    ReverseScanOrder = SQLITE_DBCONFIG_REVERSE_SCANORDER,
    EnableAttachCreate = SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE,
    EnableAttachWrite = SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE,
    EnableComments = SQLITE_DBCONFIG_ENABLE_COMMENTS,
};

enum class LimitCategory : int {
    Length = SQLITE_LIMIT_LENGTH,
    SqlLength = SQLITE_LIMIT_SQL_LENGTH,
    Column = SQLITE_LIMIT_COLUMN,
    ExprDepth = SQLITE_LIMIT_EXPR_DEPTH,
    CompoundSelect = SQLITE_LIMIT_COMPOUND_SELECT,
    VdbeOp = SQLITE_LIMIT_VDBE_OP,
    FunctionArg = SQLITE_LIMIT_FUNCTION_ARG,
    Attached = SQLITE_LIMIT_ATTACHED,
    LikePatternLength = SQLITE_LIMIT_LIKE_PATTERN_LENGTH,
    VariableNumber = SQLITE_LIMIT_VARIABLE_NUMBER,
    TriggerDepth = SQLITE_LIMIT_TRIGGER_DEPTH,
    WorkerThreads = SQLITE_LIMIT_WORKER_THREADS,
    Schema = SQLITE_LIMIT_SCHEMA,
    TriggerSteps = SQLITE_LIMIT_TRIGGER_STEPS,
};

enum class DbStatusOp : int {
    LookasideUsed = SQLITE_DBSTATUS_LOOKASIDE_USED,
    CacheUsed = SQLITE_DBSTATUS_CACHE_USED,
    SchemaUsed = SQLITE_DBSTATUS_SCHEMA_USED,
    StmtUsed = SQLITE_DBSTATUS_STMT_USED,
    LookasideHit = SQLITE_DBSTATUS_LOOKASIDE_HIT,
    LookasideMissSize = SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE,
    LookasideMissFull = SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL,
    CacheHit = SQLITE_DBSTATUS_CACHE_HIT,
    CacheMiss = SQLITE_DBSTATUS_CACHE_MISS,
    CacheWrite = SQLITE_DBSTATUS_CACHE_WRITE,
    DeferredFks = SQLITE_DBSTATUS_DEFERRED_FKS,
    CacheUsedShared = SQLITE_DBSTATUS_CACHE_USED_SHARED,
    CacheSpill = SQLITE_DBSTATUS_CACHE_SPILL,
    TempBufSpill = SQLITE_DBSTATUS_TEMPBUF_SPILL,
};

struct DbStatusResult {
    int64_t current = 0;
    int64_t highwater = 0;
};

struct ColumnMetadata {
    std::string data_type;
    std::string collation_sequence;
    bool not_null = false;
    bool primary_key = false;
    bool auto_increment = false;
};

struct TraceEvent {
    TraceFlag type = TraceFlag::Stmt;
    sqlite3_stmt* stmt = nullptr;
    std::string_view sql;
    std::chrono::nanoseconds duration{0};
    void* p = nullptr;
    void* x = nullptr;
};

// ============================================================================
// SerializedDatabase — RAII buffer for sqlite3_serialize / sqlite3_deserialize
// ============================================================================

class SQLITEX_EXPORT SerializedDatabase {
    public:
    SerializedDatabase() noexcept = default;
    ~SerializedDatabase();

    // Move-only
    SerializedDatabase(SerializedDatabase&& other) noexcept;
    SerializedDatabase& operator=(SerializedDatabase&& other) noexcept;
    SerializedDatabase(const SerializedDatabase&) = delete;
    SerializedDatabase& operator=(const SerializedDatabase&) = delete;

    /// Allocate an owned sqlite3_malloc64 buffer copied from `data`.
    [[nodiscard]] static SerializedDatabase copy_from(
        std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    [[nodiscard]] static SerializedDatabase copy_from(const R& data) {
        return copy_from(to_const_bytes(data));
    }

    explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

    [[nodiscard]] unsigned char* data() noexcept {
        return data_;
    }
    [[nodiscard]] const unsigned char* data() const noexcept {
        return data_;
    }
    [[nodiscard]] int64_t size() const noexcept {
        return size_;
    }
    [[nodiscard]] bool empty() const noexcept {
        return data_ == nullptr || size_ == 0;
    }
    [[nodiscard]] bool is_owned() const noexcept {
        return owned_;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        if (data_ == nullptr || size_ <= 0) {
            return {};
        }
        return {reinterpret_cast<const std::byte*>(data_),
                static_cast<std::size_t>(size_)};
    }

    [[nodiscard]] std::span<std::byte> writable_bytes() noexcept {
        if (data_ == nullptr || size_ <= 0) {
            return {};
        }
        return {reinterpret_cast<std::byte*>(data_),
                static_cast<std::size_t>(size_)};
    }

    /// Copy the serialized image into an owned byte vector.
    [[nodiscard]] std::vector<std::byte> to_bytes() const;

    /// Release ownership of the underlying sqlite3_malloc64 pointer.
    /// Caller becomes responsible for calling sqlite3_free() (or passing with
    /// DeserializeFlag::FreeOnClose).
    [[nodiscard]] unsigned char* release() noexcept;

    private:
    friend class Database;

    SerializedDatabase(unsigned char* data, int64_t size, bool owned) noexcept
        : data_(data), size_(size), owned_(owned) {
    }

    unsigned char* data_ = nullptr;
    int64_t size_ = 0;
    bool owned_ = false;
};

// ============================================================================
// PreupdateContext — non-owning view passed to preupdate hooks
// ============================================================================

class SQLITEX_EXPORT PreupdateContext {
    public:
    PreupdateContext(sqlite3* db, ChangeOperation op, std::string_view db_name,
                     std::string_view table_name, int64_t old_rowid,
                     int64_t new_rowid) noexcept
        : db_(db),
          op_(op),
          db_name_(db_name),
          table_name_(table_name),
          old_rowid_(old_rowid),
          new_rowid_(new_rowid) {
    }

    [[nodiscard]] ChangeOperation op() const noexcept {
        return op_;
    }
    [[nodiscard]] std::string_view db_name() const noexcept {
        return db_name_;
    }
    [[nodiscard]] std::string_view table_name() const noexcept {
        return table_name_;
    }
    [[nodiscard]] int64_t old_rowid() const noexcept {
        return old_rowid_;
    }
    [[nodiscard]] int64_t new_rowid() const noexcept {
        return new_rowid_;
    }

    /// Number of columns in the row being modified.
    [[nodiscard]] int count() const noexcept;

    /// Trigger depth (0 = direct statement, > 0 = trigger).
    [[nodiscard]] int depth() const noexcept;

    /// Column index being written by sqlite3_blob_write(), or -1 if not a blob
    /// write.
    [[nodiscard]] int blobwrite() const noexcept;

    /// Column value before the change (valid for UPDATE / DELETE).
    [[nodiscard]] Value old_value(int col) const;

    /// Column value after the change (valid for INSERT / UPDATE).
    [[nodiscard]] Value new_value(int col) const;

    private:
    sqlite3* db_;
    ChangeOperation op_;
    std::string_view db_name_;
    std::string_view table_name_;
    int64_t old_rowid_;
    int64_t new_rowid_;
};

// ============================================================================
// Database — RAII wrapper for sqlite3*
// ============================================================================

class SQLITEX_EXPORT Database {
    public:
    Database() noexcept = default;

    /// Open a database file.
    explicit Database(std::string_view path,
                      OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
                      std::string_view vfs = {});

    template <FilesystemPath Path>
    explicit Database(const Path& path,
                      OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
                      std::string_view vfs = {})
        : Database(detail::path_to_utf8(path), flags, vfs) {
    }

    ~Database();

    // Move-only
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    /// Non-throwing open factory returning std::expected<Database, Error>.
    [[nodiscard]] static Expected<Database> try_open(
        std::string_view path,
        OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
        std::string_view vfs = {});

    template <FilesystemPath Path>
    [[nodiscard]] static Expected<Database> try_open(
        const Path& path,
        OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
        std::string_view vfs = {}) {
        return try_open(detail::path_to_utf8(path), flags, vfs);
    }

    /// Explicitly open/re-open the database. Closes any previous connection.
    void open(std::string_view path,
              OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
              std::string_view vfs = {});

    template <FilesystemPath Path>
    void open(const Path& path,
              OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create,
              std::string_view vfs = {}) {
        open(detail::path_to_utf8(path), flags, vfs);
    }

    /// Close the database connection. Called automatically on destruction.
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

    /// Prepare the first SQL statement in `sql` and set `tail` to the
    /// remaining unparsed suffix of `sql`.
    [[nodiscard]] Statement prepare(std::string_view sql,
                                    std::string_view& tail);

    /// Prepare a SQL statement with PrepareFlags (sqlite3_prepare_v3).
    /// If `tail` is non-null, receives the unparsed suffix of `sql`.
    [[nodiscard]] Statement prepare_v3(std::string_view sql,
                                       PrepareFlags flags = PrepareFlag::None,
                                       std::string_view* tail = nullptr);

    /// Non-throwing prepare returning std::expected<Statement, Error>.
    [[nodiscard]] Expected<Statement> try_prepare(
        std::string_view sql, PrepareFlags flags = PrepareFlag::None,
        std::string_view* tail = nullptr);

    // === Statement Cache ===

    /// Obtain a cached (or freshly prepared) single SQL statement using this
    /// connection's built-in LRU statement cache. Automatically bypasses the
    /// cache while a ScopedPolicy is active.
    [[nodiscard]] CachedStatement prepare_cached(
        std::string_view sql, PrepareFlags flags = PrepareFlag::None);

    /// Non-throwing cached prepare returning
    /// std::expected<CachedStatement, Error>.
    [[nodiscard]] Expected<CachedStatement> try_prepare_cached(
        std::string_view sql, PrepareFlags flags = PrepareFlag::None);

    /// Access this connection's built-in thread-safe StatementCache.
    [[nodiscard]] StatementCache& statement_cache();
    [[nodiscard]] const StatementCache& statement_cache() const;

    /// Configure the maximum number of idle statements retained by this
    /// connection's built-in cache (0 disables caching and clears entries).
    void set_statement_cache_capacity(std::size_t capacity);

    /// Current capacity of this connection's built-in statement cache.
    [[nodiscard]] std::size_t statement_cache_capacity() const noexcept;

    /// Clear all idle cached statements on this connection and invalidate
    /// in-flight leases from re-entering the cache.
    void clear_statement_cache() noexcept;

    /// True if a ScopedPolicy authorization guard is currently active on this
    /// connection.
    [[nodiscard]] bool has_active_policy() const noexcept {
        return policy_ != nullptr;
    }

    // === Custom SQL Functions ===

    /// Register a scalar SQL function.
    void create_function(std::string_view name, int n_args, ScalarFunction fn,
                         FunctionFlags flags = FunctionFlag::Utf8);

    /// Register a raw scalar SQL function receiving non-owning ValueRef
    /// arguments (supports subtype, pointer-passing, nochange, etc.).
    void create_raw_function(std::string_view name, int n_args,
                             RawScalarFunction fn,
                             FunctionFlags flags = FunctionFlag::Utf8);

    /// Unregister a previously registered SQL function.
    void drop_function(std::string_view name, int n_args,
                       FunctionFlags flags = FunctionFlag::Utf8);

    /// Register an aggregate SQL function.
    void create_aggregate(
        std::string_view name, int n_args,
        std::function<std::unique_ptr<AggregateFunction>()> factory,
        FunctionFlags flags = FunctionFlag::Utf8);

    /// Register a window SQL function.
    void create_window_function(
        std::string_view name, int n_args,
        std::function<std::unique_ptr<WindowFunction>()> factory,
        FunctionFlags flags = FunctionFlag::Utf8);

    /// Convenience: register an aggregate with explicit default-initializable
    /// state type T.
    template <std::default_initializable T>
    void create_aggregate(
        std::string_view name, int n_args,
        std::function<void(FunctionContext&, T&, std::span<const Value>)> step,
        std::function<void(FunctionContext&, T&)> final,
        FunctionFlags flags = FunctionFlag::Utf8);

    /// Ensure a function name exists for virtual table xFindFunction
    /// overloading (sqlite3_overload_function).
    void overload_function(std::string_view name, int n_args);

    // === Collations ===

    using CollationCallback =
        std::function<int(std::string_view, std::string_view)>;
    using CollationNeededCallback =
        std::function<void(Database&, int text_rep, std::string_view name)>;

    /// Register a UTF-8 custom collation sequence
    /// (sqlite3_create_collation_v2).
    void create_collation(std::string_view name, CollationCallback cb);

    /// Unregister a previously registered collation sequence.
    void drop_collation(std::string_view name);

    /// Register a callback invoked when an unknown collation sequence is
    /// requested (sqlite3_collation_needed). Pass an empty callback to clear.
    void set_collation_needed(CollationNeededCallback cb);

    // === Virtual Table Modules ===

    /// Register a virtual table module.
    /// \return RAII handle; module is unregistered when destroyed.
    [[nodiscard]] ModuleRegistration create_module(
        std::string_view name,
        std::function<std::unique_ptr<VirtualTable>()> factory);

    /// Drop all virtual table modules (or all except those listed in `keep`).
    void drop_modules();
    void drop_modules(std::span<const std::string_view> keep);
    void drop_modules(std::initializer_list<std::string_view> keep) {
        drop_modules(
            std::span<const std::string_view>(keep.begin(), keep.end()));
    }

    // === Loadable Extensions ===

    /// Enable or disable sqlite3_load_extension() for this connection.
    void enable_load_extension(bool enable);

    /// Load a SQLite extension shared library (sqlite3_load_extension).
    void load_extension(std::string_view path,
                        std::string_view entry_point = {});

    template <FilesystemPath Path>
    void load_extension(const Path& path, std::string_view entry_point = {}) {
        load_extension(detail::path_to_utf8(path), entry_point);
    }

    // === Serialize / Deserialize ===

    /// Serialize a database schema into a contiguous memory buffer
    /// (sqlite3_serialize).
    [[nodiscard]] SerializedDatabase serialize(
        std::string_view schema = "main",
        SerializeFlags flags = SerializeFlag::None);

    /// Replace a database schema with an in-memory serialized image
    /// (sqlite3_deserialize).
    void deserialize(std::string_view schema, SerializedDatabase&& data,
                     DeserializeFlags flags = DeserializeFlag::FreeOnClose |
                                              DeserializeFlag::Resizeable);

    /// Deserialize from a caller-owned byte buffer. When FreeOnClose is set in
    /// `flags` (the default), the buffer is copied into SQLite-owned memory.
    void deserialize(std::string_view schema, std::span<const std::byte> data,
                     DeserializeFlags flags = DeserializeFlag::FreeOnClose |
                                              DeserializeFlag::Resizeable);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void deserialize(std::string_view schema, const R& data,
                     DeserializeFlags flags = DeserializeFlag::FreeOnClose |
                                              DeserializeFlag::Resizeable) {
        deserialize(schema, to_const_bytes(data), flags);
    }

    // === WAL ===

    /// Run a passive WAL checkpoint (sqlite3_wal_checkpoint).
    void wal_checkpoint(std::string_view db_name = {});

    /// Run a WAL checkpoint with explicit mode (sqlite3_wal_checkpoint_v2).
    CheckpointResult wal_checkpoint_v2(
        CheckpointMode mode = CheckpointMode::Passive,
        std::string_view db_name = {});

    /// Set the WAL auto-checkpoint frame threshold (0 disables).
    void wal_autocheckpoint(int pages);

    using WalHook =
        std::function<int(Database&, std::string_view db_name, int pages)>;

    /// Register a WAL commit hook (sqlite3_wal_hook). Pass empty to clear.
    void set_wal_hook(WalHook cb);

    // === Hooks and Handlers ===

    using UpdateHook =
        std::function<void(ChangeOperation op, std::string_view db_name,
                           std::string_view table, int64_t rowid)>;
    /// Return true to allow commit, false to convert the commit into ROLLBACK.
    using CommitHook = std::function<bool()>;
    using RollbackHook = std::function<void()>;
    using PreupdateHook = std::function<void(const PreupdateContext&)>;
    using TraceHook = std::function<void(const TraceEvent&)>;
    /// Return true to interrupt execution, false to continue.
    using ProgressHandler = std::function<bool()>;
    /// Return true to retry, false to stop waiting and return SQLITE_BUSY.
    using BusyHandler = std::function<bool(int retries)>;
    using AutovacuumPagesHook = std::function<unsigned int(
        std::string_view schema, unsigned int db_pages, unsigned int free_pages,
        unsigned int page_size)>;
    using UnlockNotifyHook = std::function<void()>;

    void set_update_hook(UpdateHook cb);
    void set_commit_hook(CommitHook cb);
    void set_rollback_hook(RollbackHook cb);
    void set_preupdate_hook(PreupdateHook cb);
    void set_trace_hook(TraceFlags mask, TraceHook cb);
    void set_progress_handler(int n_ops, ProgressHandler cb);
    void set_busy_handler(BusyHandler cb);
    void set_autovacuum_pages(AutovacuumPagesHook cb);
    void unlock_notify(UnlockNotifyHook cb);

    /// Configure blocking lock timeout (sqlite3_setlk_timeout).
    void setlk_timeout(std::chrono::milliseconds timeout, int flags = 0);

    // === Query Execution ===

    /// Execute one or more semicolon-separated SQL statements (no parameters).
    void execute(std::string_view sql);

    /// Prepare, bind `args...`, and step a single parameterized SQL statement.
    template <Bindable... Args>
        requires(sizeof...(Args) >= 1)
    void execute(std::string_view sql, Args&&... args);

    /// Execute SQL with a type-safe per-row callback.
    /// NULL values are delivered as std::nullopt. Return false to stop early.
    using RowCallback = std::function<bool(
        std::span<const std::optional<std::string_view>> values,
        std::span<const std::string_view> names)>;

    void exec(std::string_view sql, RowCallback cb);

    /// Execute SQL and collect all rows as Value vectors.
    [[nodiscard]] std::vector<std::vector<Value>> query(std::string_view sql);

    /// Execute parameterized SQL and collect all rows as Value vectors.
    template <Bindable... Args>
        requires(sizeof...(Args) >= 1)
    [[nodiscard]] std::vector<std::vector<Value>> query(std::string_view sql,
                                                        Args&&... args);

    /// Execute SQL (with optional bound parameters) and return the first
    /// column of the first row. Constrained to OwnedColumnValue so non-owning
    /// views (std::string_view, std::span) cannot dangle.
    template <OwnedColumnValue T = int64_t, Bindable... Args>
    [[nodiscard]] std::optional<T> query_value(std::string_view sql,
                                               Args&&... args);

    // === Transactions & Savepoints ===

    void begin_transaction(TransactionMode mode = TransactionMode::Deferred);
    void commit();
    void rollback();

    /// RAII transaction guard.
    /// Rolls back on destruction unless commit() was explicitly called.
    class [[nodiscard]] Transaction {
        public:
        explicit Transaction(Database& db,
                             TransactionMode mode = TransactionMode::Deferred)
            : db_(&db) {
            db_->begin_transaction(mode);
        }
        ~Transaction() {
            if (db_ != nullptr && !db_->is_autocommit()) {
                try {
                    db_->execute("ROLLBACK");
                } catch (...) {
                    // Destructors must not throw.
                }
            }
        }

        Transaction(Transaction&& other) noexcept
            : db_(std::exchange(other.db_, nullptr)) {
        }
        Transaction& operator=(Transaction&& other) noexcept {
            if (this != &other) {
                if (db_ != nullptr && !db_->is_autocommit()) {
                    try {
                        db_->execute("ROLLBACK");
                    } catch (...) {
                        // Must not throw during move-assignment.
                    }
                }
                db_ = std::exchange(other.db_, nullptr);
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
                Database* db = std::exchange(db_, nullptr);
                db->execute("ROLLBACK");
            }
        }

        private:
        Database* db_ = nullptr;
    };

    /// RAII savepoint guard (supports nested transactions).
    /// Rolls back and releases on destruction unless release() was explicitly
    /// called.
    class [[nodiscard]] SQLITEX_EXPORT Savepoint {
        public:
        Savepoint(Database& db, std::string_view name);
        ~Savepoint();

        Savepoint(Savepoint&& other) noexcept;
        Savepoint& operator=(Savepoint&& other) noexcept;
        Savepoint(const Savepoint&) = delete;
        Savepoint& operator=(const Savepoint&) = delete;

        /// Release (commit) this savepoint.
        void release();

        /// Roll back to this savepoint and release it.
        void rollback();

        /// Roll back changes since this savepoint while keeping it active.
        void rollback_to();

        private:
        Database* db_ = nullptr;
        std::string quoted_name_;
    };

    /// Convenience factories returning RAII transaction / savepoint guards.
    [[nodiscard]] Transaction transaction(
        TransactionMode mode = TransactionMode::Deferred) {
        return Transaction(*this, mode);
    }

    [[nodiscard]] Savepoint savepoint(std::string_view name) {
        return Savepoint(*this, name);
    }

    // === Configuration, Limits & Status ===

    /// Enable or disable a boolean SQLITE_DBCONFIG_* option.
    /// Returns the resulting state reported by SQLite.
    bool db_config(DbConfig op, bool enable);

    /// Query a boolean SQLITE_DBCONFIG_* option without modifying it.
    [[nodiscard]] bool db_config_get(DbConfig op) const;

    /// Set the name of the "main" database schema (SQLITE_DBCONFIG_MAINDBNAME).
    /// The Database owns the string storage for the lifetime of the connection.
    void set_main_db_name(std::string_view name);

    /// Configure the per-connection lookaside memory allocator
    /// (SQLITE_DBCONFIG_LOOKASIDE).
    void set_lookaside(void* buf, int slot_size, int slot_count);

    /// Configure floating-point formatting digits (3..23) or pass 0 to query
    /// (SQLITE_DBCONFIG_FP_DIGITS).
    int set_fp_digits(int digits);
    [[nodiscard]] int fp_digits() const;

    /// Query or change a per-connection runtime limit (sqlite3_limit).
    /// Pass new_val < 0 to query without modifying. Returns the previous limit.
    int limit(LimitCategory id, int new_val = -1);

    /// Query per-connection status metrics (sqlite3_db_status64).
    [[nodiscard]] DbStatusResult db_status(DbStatusOp op,
                                           bool reset_highwater = false) const;

    // === Utility & Introspection ===

    [[nodiscard]] int64_t last_insert_rowid() const noexcept;
    void set_last_insert_rowid(int64_t rowid) noexcept;
    [[nodiscard]] int changes() const noexcept;
    [[nodiscard]] int64_t changes64() const noexcept;
    [[nodiscard]] int64_t total_changes() const noexcept;
    [[nodiscard]] int64_t total_changes64() const noexcept;

    [[nodiscard]] int errcode() const noexcept;
    [[nodiscard]] int extended_errcode() const noexcept;
    [[nodiscard]] std::string_view errmsg() const noexcept;
    [[nodiscard]] int error_offset() const noexcept;
    [[nodiscard]] int system_errno() const noexcept;
    void set_extended_result_codes(bool enable);
    int set_errmsg(int errcode, std::string_view msg = {});

    /// Interrupt a long-running query (safe to call from another thread).
    void interrupt();
    [[nodiscard]] bool is_interrupted() const noexcept;

    /// Set a busy timeout duration.
    void set_busy_timeout(std::chrono::milliseconds timeout);

    /// Check if in autocommit mode.
    [[nodiscard]] bool is_autocommit() const noexcept;

    /// Query current transaction state (sqlite3_txn_state).
    /// Pass empty `schema` to query the highest state across all schemas.
    [[nodiscard]] TxnState txn_state(
        std::string_view schema = {}) const noexcept;

    /// Query read-only status of a schema (sqlite3_db_readonly).
    [[nodiscard]] DbReadOnlyState db_readonly(
        std::string_view schema = "main") const noexcept;
    [[nodiscard]] bool is_readonly(
        std::string_view schema = "main") const noexcept;

    /// Get the filename for an attached database.
    [[nodiscard]] std::string_view db_filename(
        std::string_view db_name = "main") const;

    /// Get the schema name at 0-based index (sqlite3_db_name).
    [[nodiscard]] std::optional<std::string_view> db_name(
        int index) const noexcept;

    /// Return all attached schema names on this connection.
    [[nodiscard]] std::vector<std::string> db_names() const;

    /// Flush dirty pager cache pages to disk (sqlite3_db_cacheflush).
    void db_cacheflush();

    /// Attempt to free unused memory held by this connection
    /// (sqlite3_db_release_memory).
    void db_release_memory();

    /// Iterate active prepared statements (sqlite3_next_stmt).
    [[nodiscard]] sqlite3_stmt* next_stmt(
        sqlite3_stmt* prev = nullptr) const noexcept;
    [[nodiscard]] std::vector<sqlite3_stmt*> active_statements() const;

    /// Query column schema metadata (sqlite3_table_column_metadata).
    [[nodiscard]] ColumnMetadata table_column_metadata(
        std::string_view table, std::string_view column,
        std::string_view db_name = {}) const;

    /// Check whether a table exists via sqlite3_table_column_metadata.
    [[nodiscard]] bool table_exists(std::string_view table,
                                    std::string_view db_name = {}) const;

    /// Attach or retrieve arbitrary client data keyed by name
    /// (sqlite3_set_clientdata / sqlite3_get_clientdata).
    void set_clientdata(std::string_view name, void* data,
                        void (*destructor)(void*) = nullptr);

    /// Type-safe owning client data attachment.
    template <typename T>
    void set_clientdata(std::string_view name, std::unique_ptr<T> data) {
        set_clientdata(name, data.release(), &detail::delete_typed<T>);
    }

    [[nodiscard]] void* get_clientdata(std::string_view name) const noexcept;

    /// Type-safe client data retrieval.
    template <typename T>
    [[nodiscard]] T* get_clientdata_as(std::string_view name) const noexcept {
        return static_cast<T*>(get_clientdata(name));
    }

    /// Low-level VFS file control escape hatch (sqlite3_file_control).
    /// Returns the raw SQLite result code (SQLITE_OK, SQLITE_NOTFOUND, etc.).
    int file_control(std::string_view schema, int op, void* arg);

    private:
    struct HookState;
    struct SQLITEX_EXPORT HookStateDeleter {
        void operator()(HookState* p) noexcept;
    };

    struct SQLITEX_EXPORT Deleter {
        void operator()(sqlite3* db) noexcept;
    };
    // Declare hooks_ BEFORE db_holder_ so hooks remain alive while
    // sqlite3_close_v2() runs (e.g., for SQLITE_TRACE_CLOSE callbacks).
    // Declare stmt_cache_ AFTER db_holder_ so cached statements are finalized
    // before sqlite3_close_v2() runs on destruction.
    std::unique_ptr<HookState, HookStateDeleter> hooks_{};
    std::unique_ptr<sqlite3, Deleter> db_holder_{};
    StatementCache stmt_cache_{};

    HookState& ensure_hooks();
    StatementCache& ensure_statement_cache();

    friend struct detail::PolicyAccess;
    /// Innermost active ScopedPolicy; guards must not outlive the connection.
    ScopedPolicy* policy_ = nullptr;
};

// ============================================================================
// Template implementations
// ============================================================================

template <Bindable... Args>
    requires(sizeof...(Args) >= 1)
void Database::execute(std::string_view sql, Args&&... args) {
    Statement stmt = prepare(sql);
    stmt.bind_all(std::forward<Args>(args)...);
    (void)stmt.step();
}

template <Bindable... Args>
    requires(sizeof...(Args) >= 1)
std::vector<std::vector<Value>> Database::query(std::string_view sql,
                                                Args&&... args) {
    Statement stmt = prepare(sql);
    stmt.bind_all(std::forward<Args>(args)...);
    std::vector<std::vector<Value>> rows;
    while (stmt.step()) {
        int const ncols = stmt.column_count();
        std::vector<Value> row;
        row.reserve(static_cast<std::size_t>(ncols));
        for (int i = 0; i < ncols; ++i) {
            row.push_back(stmt.column_value(i));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

template <OwnedColumnValue T, Bindable... Args>
std::optional<T> Database::query_value(std::string_view sql, Args&&... args) {
    Statement stmt = prepare(sql);
    if constexpr (sizeof...(Args) > 0) {
        stmt.bind_all(std::forward<Args>(args)...);
    }
    if (!stmt.step() || stmt.column_count() == 0) {
        return std::nullopt;
    }
    using Decayed = std::remove_cvref_t<T>;
    if constexpr (!std::same_as<Decayed, Value> &&
                  !detail::OptionalInner<Decayed>::is_optional) {
        if (stmt.column_is_null(0)) {
            return std::nullopt;
        }
    }
    return stmt.column_as<T>(0);
}

template <std::default_initializable T>
void Database::create_aggregate(
    std::string_view name, int n_args,
    std::function<void(FunctionContext&, T&, std::span<const Value>)> step,
    std::function<void(FunctionContext&, T&)> final, FunctionFlags flags) {
    struct TypedAggregate : public AggregateFunction {
        T state_{};
        std::function<void(FunctionContext&, T&, std::span<const Value>)>
            step_fn{};
        std::function<void(FunctionContext&, T&)> final_fn{};

        void step(FunctionContext& ctx, std::span<const Value> args) override {
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
