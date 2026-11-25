#include "sqlitex/database.h"
#include "sqlitex/error.h"
#include "sqlitex/statement.h"
#include "sqlitex/virtual_table.h"

#include "policy_internal.h"

#include <sqlite3.h>

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// HookState — move-stable storage for per-connection callbacks and strings
// ============================================================================

struct Database::HookState {
    Database* owner = nullptr;
    Database::UpdateHook update_hook;
    Database::CommitHook commit_hook;
    Database::RollbackHook rollback_hook;
    Database::PreupdateHook preupdate_hook;
    Database::TraceHook trace_hook;
    Database::ProgressHandler progress_handler;
    Database::BusyHandler busy_handler;
    Database::WalHook wal_hook;
    Database::CollationNeededCallback collation_needed;
    Database::UnlockNotifyHook unlock_notify;
    std::string main_db_name;

    static ChangeOperation map_change_op(int op) noexcept {
        switch (op) {
        case SQLITE_INSERT:
            return ChangeOperation::Insert;
        case SQLITE_DELETE:
            return ChangeOperation::Delete;
        case SQLITE_UPDATE:
        default:
            return ChangeOperation::Update;
        }
    }

    static TraceFlag map_trace_type(unsigned int type) noexcept {
        switch (type) {
        case SQLITE_TRACE_STMT:
            return TraceFlag::Stmt;
        case SQLITE_TRACE_PROFILE:
            return TraceFlag::Profile;
        case SQLITE_TRACE_ROW:
            return TraceFlag::Row;
        case SQLITE_TRACE_CLOSE:
            return TraceFlag::Close;
        default:
            return TraceFlag::None;
        }
    }

    static void update_hook_shim(void* pCtx, int op, const char* zDb,
                                 const char* zTable,
                                 sqlite3_int64 rowid) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->update_hook) {
            return;
        }
        try {
            state->update_hook(
                map_change_op(op),
                zDb ? std::string_view(zDb) : std::string_view{},
                zTable ? std::string_view(zTable) : std::string_view{},
                static_cast<int64_t>(rowid));
        } catch (...) {
            // C hook must not unwind across SQLite frames.
        }
    }

    static int commit_hook_shim(void* pCtx) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->commit_hook) {
            return 0;
        }
        try {
            // C++ callback returns true to allow commit; SQLite expects 0.
            return state->commit_hook() ? 0 : 1;
        } catch (...) {
            return 1; // Convert uncaught exception into rollback.
        }
    }

    static void rollback_hook_shim(void* pCtx) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->rollback_hook) {
            return;
        }
        try {
            state->rollback_hook();
        } catch (...) {
            // C hook must not unwind across SQLite frames.
        }
    }

    static void preupdate_hook_shim(void* pCtx, sqlite3* db, int op,
                                    const char* zDb, const char* zName,
                                    sqlite3_int64 iKey1,
                                    sqlite3_int64 iKey2) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->preupdate_hook) {
            return;
        }
        try {
            PreupdateContext ctx(
                db, map_change_op(op),
                zDb ? std::string_view(zDb) : std::string_view{},
                zName ? std::string_view(zName) : std::string_view{},
                static_cast<int64_t>(iKey1), static_cast<int64_t>(iKey2));
            state->preupdate_hook(ctx);
        } catch (...) {
            // C hook must not unwind across SQLite frames.
        }
    }

    static int trace_hook_shim(unsigned int type, void* pCtx, void* p,
                               void* x) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->trace_hook) {
            return 0;
        }
        try {
            TraceEvent ev;
            ev.type = map_trace_type(type);
            ev.p = p;
            ev.x = x;
            if (type == SQLITE_TRACE_STMT) {
                ev.stmt = static_cast<sqlite3_stmt*>(p);
                if (x != nullptr) {
                    ev.sql = std::string_view(static_cast<const char*>(x));
                }
            } else if (type == SQLITE_TRACE_PROFILE) {
                ev.stmt = static_cast<sqlite3_stmt*>(p);
                if (x != nullptr) {
                    ev.duration = std::chrono::nanoseconds(
                        static_cast<int64_t>(*static_cast<sqlite3_int64*>(x)));
                }
            } else if (type == SQLITE_TRACE_ROW) {
                ev.stmt = static_cast<sqlite3_stmt*>(p);
            }
            state->trace_hook(ev);
        } catch (...) {
            // C hook must not unwind across SQLite frames.
        }
        return 0;
    }

    static int progress_handler_shim(void* pCtx) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->progress_handler) {
            return 0;
        }
        try {
            return state->progress_handler() ? 1 : 0;
        } catch (...) {
            return 1;
        }
    }

    static int busy_handler_shim(void* pCtx, int retries) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->busy_handler) {
            return 0;
        }
        try {
            return state->busy_handler(retries) ? 1 : 0;
        } catch (...) {
            return 0;
        }
    }

    static int wal_hook_shim(void* pCtx, sqlite3* /*db*/, const char* zDb,
                             int pages) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->wal_hook || state->owner == nullptr) {
            return SQLITE_OK;
        }
        try {
            return state->wal_hook(
                *state->owner, zDb ? std::string_view(zDb) : std::string_view{},
                pages);
        } catch (...) {
            return SQLITE_ERROR;
        }
    }

    static void unlock_notify_shim(void** apArg, int nArg) noexcept {
        for (int i = 0; i < nArg; ++i) {
            auto* state = static_cast<HookState*>(apArg[i]);
            if (state != nullptr && state->unlock_notify) {
                try {
                    state->unlock_notify();
                } catch (...) {
                    // C callback must not unwind.
                }
            }
        }
    }

    static void collation_needed_shim(void* pCtx, sqlite3* /*db*/, int eTextRep,
                                      const char* zName) noexcept {
        auto* state = static_cast<HookState*>(pCtx);
        if (state == nullptr || !state->collation_needed ||
            state->owner == nullptr) {
            return;
        }
        try {
            state->collation_needed(
                *state->owner, eTextRep,
                zName ? std::string_view(zName) : std::string_view{});
        } catch (...) {
            // C callback must not unwind.
        }
    }
};

void Database::HookStateDeleter::operator()(HookState* p) noexcept {
    delete p;
}

Database::HookState& Database::ensure_hooks() {
    if (!hooks_) {
        hooks_.reset(new HookState());
        hooks_->owner = this;
    }
    return *hooks_;
}

// ============================================================================
// SerializedDatabase
// ============================================================================

SerializedDatabase::~SerializedDatabase() {
    if (owned_ && data_ != nullptr) {
        sqlite3_free(data_);
    }
}

SerializedDatabase::SerializedDatabase(SerializedDatabase&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      owned_(std::exchange(other.owned_, false)) {
}

SerializedDatabase& SerializedDatabase::operator=(
    SerializedDatabase&& other) noexcept {
    if (this != &other) {
        if (owned_ && data_ != nullptr) {
            sqlite3_free(data_);
        }
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        owned_ = std::exchange(other.owned_, false);
    }
    return *this;
}

SerializedDatabase SerializedDatabase::copy_from(
    std::span<const std::byte> data) {
    auto alloc_sz =
        static_cast<sqlite3_uint64>(!data.empty() ? data.size() : 1);
    auto* buf = static_cast<unsigned char*>(sqlite3_malloc64(alloc_sz));
    if (buf == nullptr) {
        throw_sqlite_error(SQLITE_NOMEM, nullptr);
    }
    if (!data.empty()) {
        std::memcpy(buf, data.data(), data.size());
    }
    return SerializedDatabase(buf, static_cast<int64_t>(data.size()), true);
}

std::vector<std::byte> SerializedDatabase::to_bytes() const {
    auto view = bytes();
    return std::vector<std::byte>(view.begin(), view.end());
}

unsigned char* SerializedDatabase::release() noexcept {
    owned_ = false;
    size_ = 0;
    return std::exchange(data_, nullptr);
}

// ============================================================================
// PreupdateContext
// ============================================================================

int PreupdateContext::count() const noexcept {
    return sqlite3_preupdate_count(db_);
}

int PreupdateContext::depth() const noexcept {
    return sqlite3_preupdate_depth(db_);
}

int PreupdateContext::blobwrite() const noexcept {
    return sqlite3_preupdate_blobwrite(db_);
}

Value PreupdateContext::old_value(int col) const {
    sqlite3_value* val = nullptr;
    int rc = sqlite3_preupdate_old(db_, col, &val);
    throw_on_error(rc, db_);
    return val ? Value::from_sqlite3_value(val) : Value{};
}

Value PreupdateContext::new_value(int col) const {
    sqlite3_value* val = nullptr;
    int rc = sqlite3_preupdate_new(db_, col, &val);
    throw_on_error(rc, db_);
    return val ? Value::from_sqlite3_value(val) : Value{};
}

// ============================================================================
// Savepoint
// ============================================================================

namespace {

std::string quote_sql_identifier(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for (char c : name) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // anonymous namespace

Database::Savepoint::Savepoint(Database& db, std::string_view name)
    : db_(&db), quoted_name_(quote_sql_identifier(name)) {
    db_->execute("SAVEPOINT " + quoted_name_);
}

Database::Savepoint::~Savepoint() {
    if (db_ != nullptr) {
        try {
            db_->execute("ROLLBACK TO SAVEPOINT " + quoted_name_);
            db_->execute("RELEASE SAVEPOINT " + quoted_name_);
        } catch (...) {
            // Destructors must not throw.
        }
    }
}

Database::Savepoint::Savepoint(Savepoint&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      quoted_name_(std::move(other.quoted_name_)) {
}

Database::Savepoint& Database::Savepoint::operator=(
    Savepoint&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            try {
                db_->execute("ROLLBACK TO SAVEPOINT " + quoted_name_);
                db_->execute("RELEASE SAVEPOINT " + quoted_name_);
            } catch (...) {
                // Destructors/moves must not throw.
            }
        }
        db_ = std::exchange(other.db_, nullptr);
        quoted_name_ = std::move(other.quoted_name_);
    }
    return *this;
}

void Database::Savepoint::release() {
    if (db_ != nullptr) {
        db_->execute("RELEASE SAVEPOINT " + quoted_name_);
        db_ = nullptr;
    }
}

void Database::Savepoint::rollback() {
    if (db_ != nullptr) {
        Database* db = std::exchange(db_, nullptr);
        db->execute("ROLLBACK TO SAVEPOINT " + quoted_name_);
        db->execute("RELEASE SAVEPOINT " + quoted_name_);
    }
}

void Database::Savepoint::rollback_to() {
    if (db_ != nullptr) {
        db_->execute("ROLLBACK TO SAVEPOINT " + quoted_name_);
    }
}

// ============================================================================
// Construction / Destruction
// ============================================================================

Database::Database(std::string_view path, OpenFlags flags,
                   std::string_view vfs) {
    open(path, flags, vfs);
}

Database::~Database() {
    assert(policy_ == nullptr &&
           "Database destroyed while a ScopedPolicy is active");
}

Database::Database(Database&& other) noexcept
    : hooks_(std::move(other.hooks_)), db_holder_(std::move(other.db_holder_)) {
    assert(other.policy_ == nullptr &&
           "Database moved while a ScopedPolicy is active");
    if (hooks_) {
        hooks_->owner = this;
    }
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        assert(other.policy_ == nullptr &&
               "Database moved while a ScopedPolicy is active");
        close();
        hooks_ = std::move(other.hooks_);
        db_holder_ = std::move(other.db_holder_);
        if (hooks_) {
            hooks_->owner = this;
        }
    }
    return *this;
}

Expected<Database> Database::try_open(std::string_view path, OpenFlags flags,
                                      std::string_view vfs) {
    sqlite3* raw = nullptr;
    std::string path_str(path);
    std::string vfs_str(vfs);
    const char* vfs_name = vfs.empty() ? nullptr : vfs_str.c_str();

    int const rc =
        sqlite3_open_v2(path_str.c_str(), &raw, flags.raw(), vfs_name);
    if (rc != SQLITE_OK) {
        std::string msg =
            raw != nullptr ? std::string(sqlite3_errmsg(raw))
                           : (std::string("Cannot open database: ") + path_str);
        if (raw != nullptr) {
            sqlite3_close_v2(raw);
        }
        return std::unexpected(make_sqlite_error(rc, msg));
    }

    Database db;
    db.db_holder_.reset(raw);
    return db;
}

void Database::open(std::string_view path, OpenFlags flags,
                    std::string_view vfs) {
    close();

    sqlite3* raw = nullptr;
    // string_view::data() is not null-terminated; copy to std::string first.
    std::string path_str(path);
    std::string vfs_str(vfs);
    const char* vfs_name = vfs.empty() ? nullptr : vfs_str.c_str();

    int rc = sqlite3_open_v2(path_str.c_str(), &raw, flags.raw(), vfs_name);
    if (rc != SQLITE_OK) {
        if (raw != nullptr) {
            // Capture the error message before closing the handle, then throw
            // via throw_on_error so the correct Error subclass is raised.
            std::string msg(sqlite3_errmsg(raw));
            sqlite3_close_v2(raw);
            throw_on_error(rc, msg);
        }
        throw_on_error(rc, std::string("Cannot open database: ") + path_str);
    }

    db_holder_.reset(raw);
}

void Database::close() noexcept {
    assert(policy_ == nullptr &&
           "Database closed while a ScopedPolicy is active");
    // Close the sqlite3* handle BEFORE releasing hooks_ so close trace hooks
    // can safely fire during sqlite3_close_v2().
    db_holder_.reset();
    hooks_.reset();
}

// ============================================================================
// Query Execution
// ============================================================================

Statement Database::prepare(std::string_view sql) {
    return prepare_v3(sql, PrepareFlag::None, nullptr);
}

Statement Database::prepare(std::string_view sql, std::string_view& tail) {
    return prepare_v3(sql, PrepareFlag::None, &tail);
}

Statement Database::prepare_v3(std::string_view sql, PrepareFlags flags,
                               std::string_view* tail) {
    sqlite3_stmt* stmt = nullptr;
    const char* raw_tail = nullptr;
    std::string sql_str(sql);

    detail::AuthSink sink(db_holder_.get());
    int rc = sqlite3_prepare_v3(db_holder_.get(), sql_str.c_str(),
                                static_cast<int>(sql_str.size()), flags.raw(),
                                &stmt, &raw_tail);
    sink.check(rc);
    throw_on_error(rc, db_holder_.get());

    if (tail != nullptr) {
        if (raw_tail != nullptr) {
            auto offset = static_cast<std::size_t>(raw_tail - sql_str.c_str());
            *tail = (offset <= sql.size()) ? sql.substr(offset)
                                           : std::string_view{};
        } else {
            *tail = std::string_view{};
        }
    }

    return {stmt, this};
}

Expected<Statement> Database::try_prepare(std::string_view sql,
                                          PrepareFlags flags,
                                          std::string_view* tail) {
    sqlite3_stmt* stmt = nullptr;
    const char* raw_tail = nullptr;
    std::string sql_str(sql);

    detail::AuthSink sink(db_holder_.get());
    int const rc = sqlite3_prepare_v3(db_holder_.get(), sql_str.c_str(),
                                      static_cast<int>(sql_str.size()),
                                      flags.raw(), &stmt, &raw_tail);
    if (rc != SQLITE_OK) {
        return std::unexpected(make_sqlite_error(rc, db_holder_.get()));
    }

    if (tail != nullptr) {
        if (raw_tail != nullptr) {
            auto offset = static_cast<std::size_t>(raw_tail - sql_str.c_str());
            *tail = (offset <= sql.size()) ? sql.substr(offset)
                                           : std::string_view{};
        } else {
            *tail = std::string_view{};
        }
    }

    return Statement{stmt, this};
}

void Database::execute(std::string_view sql) {
    std::string sql_str(sql);
    char* err_msg = nullptr;

    detail::AuthSink sink(db_holder_.get());
    int rc = sqlite3_exec(db_holder_.get(), sql_str.c_str(), nullptr, nullptr,
                          &err_msg);
    if (rc != SQLITE_OK) {
        // err_msg may carry more detail than sqlite3_errmsg; use it when
        // present.
        std::string msg(err_msg ? err_msg : sqlite3_errmsg(db_holder_.get()));
        sqlite3_free(err_msg);
        sink.check(rc, msg.c_str());
        throw_on_error(rc, msg.c_str());
    }
}

void Database::exec(std::string_view sql, RowCallback cb) {
    std::string sql_str(sql);

    // Context passed through the C shim: callback pointer + pending exception.
    struct ExecCtx {
        RowCallback* cb;
        std::exception_ptr* exc;
    };
    std::exception_ptr pending_exc;
    ExecCtx ctx{&cb, &pending_exc};

    // The lambda must not capture anything so it is convertible to a
    // plain function pointer.  All state flows through pCtx.
    // Exceptions are caught here so they never propagate through the C
    // boundary (which would be undefined behaviour).
    auto exec_cb = [](void* pCtx, int ncols, char** values,
                      char** names) -> int {
        auto* ec = static_cast<ExecCtx*>(pCtx);
        try {
            std::vector<std::optional<std::string_view>> val_views;
            std::vector<std::string_view> name_views;
            val_views.reserve(static_cast<std::size_t>(ncols));
            name_views.reserve(static_cast<std::size_t>(ncols));
            for (int i = 0; i < ncols; ++i) {
                if (values != nullptr && values[i] != nullptr) {
                    val_views.emplace_back(std::string_view(values[i]));
                } else {
                    val_views.emplace_back(std::nullopt);
                }
                name_views.emplace_back(
                    (names != nullptr && names[i] != nullptr)
                        ? std::string_view(names[i])
                        : std::string_view{});
            }
            return (*ec->cb)(val_views, name_views) ? 0 : 1;
        } catch (...) {
            *ec->exc = std::current_exception();
            return 1; // signal SQLite to abort
        }
    };

    char* err_msg = nullptr;
    detail::AuthSink sink(db_holder_.get());
    int rc = sqlite3_exec(db_holder_.get(), sql_str.c_str(), exec_cb, &ctx,
                          &err_msg);
    sqlite3_free(err_msg);

    // Re-throw any C++ exception raised inside the callback.
    if (pending_exc) {
        std::rethrow_exception(pending_exc);
    }
    sink.check(rc);

    // SQLITE_ABORT is the normal result when the callback returned false to
    // stop early.  Treat it as a clean stop rather than an error.
    if (rc == SQLITE_ABORT) {
        return;
    }

    if (rc != SQLITE_OK) {
        throw_sqlite_error(rc, db_holder_.get());
    }
}

std::vector<std::vector<Value>> Database::query(std::string_view sql) {
    Statement stmt = prepare(sql);
    std::vector<std::vector<Value>> rows;

    while (stmt.step()) {
        int ncols = stmt.column_count();
        std::vector<Value> row;
        row.reserve(static_cast<std::size_t>(ncols));
        for (int i = 0; i < ncols; ++i) {
            row.push_back(stmt.column_value(i));
        }
        rows.push_back(std::move(row));
    }

    return rows;
}

// ============================================================================
// Transactions
// ============================================================================

void Database::begin_transaction(TransactionMode mode) {
    switch (mode) {
    case TransactionMode::Deferred:
        execute("BEGIN DEFERRED");
        break;
    case TransactionMode::Immediate:
        execute("BEGIN IMMEDIATE");
        break;
    case TransactionMode::Exclusive:
        execute("BEGIN EXCLUSIVE");
        break;
    }
}

void Database::commit() {
    execute("COMMIT");
}

void Database::rollback() {
    execute("ROLLBACK");
}

// ============================================================================
// Serialize / Deserialize
// ============================================================================

SerializedDatabase Database::serialize(std::string_view schema,
                                       SerializeFlags flags) {
    std::string schema_str(schema);
    sqlite3_int64 sz = 0;
    unsigned char* buf = sqlite3_serialize(db_holder_.get(), schema_str.c_str(),
                                           &sz, flags.raw());
    if (buf == nullptr) {
        if (flags.contains(SerializeFlag::NoCopy)) {
            return SerializedDatabase{};
        }
        int rc = sqlite3_errcode(db_holder_.get());
        throw_sqlite_error(rc != SQLITE_OK ? rc : SQLITE_ERROR,
                           db_holder_.get());
    }
    bool const owned = !flags.contains(SerializeFlag::NoCopy);
    return SerializedDatabase(buf, static_cast<int64_t>(sz), owned);
}

void Database::deserialize(std::string_view schema, SerializedDatabase&& data,
                           DeserializeFlags flags) {
    if (db_holder_ == nullptr) {
        throw MisuseError(ErrorCode::Misuse,
                          "Database::deserialize() on closed database");
    }
    if (data.size() < 0) {
        throw MisuseError(ErrorCode::Misuse,
                          "Database::deserialize(): negative size");
    }

    std::string schema_str(schema);
    int64_t sz = data.size();
    unsigned char* raw = nullptr;

    if (flags.contains(DeserializeFlag::FreeOnClose)) {
        if (!data.is_owned()) {
            SerializedDatabase owned_copy =
                SerializedDatabase::copy_from(data.bytes());
            raw = owned_copy.release();
        } else {
            // Release before calling sqlite3_deserialize: SQLite frees `raw`
            // on both close and error when FREEONCLOSE is set.
            raw = data.release();
        }
    } else {
        raw = data.data();
    }

    int rc = sqlite3_deserialize(db_holder_.get(), schema_str.c_str(), raw,
                                 static_cast<sqlite3_int64>(sz),
                                 static_cast<sqlite3_int64>(sz), flags.raw());
    throw_on_error(rc, db_holder_.get());
}

void Database::deserialize(std::string_view schema,
                           std::span<const std::byte> data,
                           DeserializeFlags flags) {
    if (db_holder_ == nullptr) {
        throw MisuseError(ErrorCode::Misuse,
                          "Database::deserialize() on closed database");
    }
    if (flags.contains(DeserializeFlag::FreeOnClose)) {
        SerializedDatabase copy = SerializedDatabase::copy_from(data);
        deserialize(schema, std::move(copy), flags);
        return;
    }
    std::string schema_str(schema);
    int rc = sqlite3_deserialize(
        db_holder_.get(), schema_str.c_str(),
        reinterpret_cast<unsigned char*>(const_cast<std::byte*>(data.data())),
        static_cast<sqlite3_int64>(data.size()),
        static_cast<sqlite3_int64>(data.size()), flags.raw());
    throw_on_error(rc, db_holder_.get());
}

// ============================================================================
// WAL
// ============================================================================

void Database::wal_checkpoint(std::string_view db_name) {
    std::string name(db_name);
    const char* z_db = db_name.empty() ? nullptr : name.c_str();
    int rc = sqlite3_wal_checkpoint(db_holder_.get(), z_db);
    throw_on_error(rc, db_holder_.get());
}

CheckpointResult Database::wal_checkpoint_v2(CheckpointMode mode,
                                             std::string_view db_name) {
    std::string name(db_name);
    const char* z_db = db_name.empty() ? nullptr : name.c_str();
    CheckpointResult res;
    int rc = sqlite3_wal_checkpoint_v2(db_holder_.get(), z_db,
                                       static_cast<int>(mode), &res.log_frames,
                                       &res.checkpointed_frames);
    throw_on_error(rc, db_holder_.get());
    return res;
}

void Database::wal_autocheckpoint(int pages) {
    int rc = sqlite3_wal_autocheckpoint(db_holder_.get(), pages);
    throw_on_error(rc, db_holder_.get());
}

// ============================================================================
// Hooks & Handlers
// ============================================================================

namespace {

unsigned int autovacuum_pages_shim(void* pCtx, const char* zSchema,
                                   unsigned int db_pages,
                                   unsigned int free_pages,
                                   unsigned int page_size) noexcept {
    auto* cb = static_cast<Database::AutovacuumPagesHook*>(pCtx);
    if (cb == nullptr || !(*cb)) {
        return 0;
    }
    try {
        return (*cb)(zSchema ? std::string_view(zSchema) : std::string_view{},
                     db_pages, free_pages, page_size);
    } catch (...) {
        return 0;
    }
}

void autovacuum_pages_destroy(void* pCtx) noexcept {
    delete static_cast<Database::AutovacuumPagesHook*>(pCtx);
}

int collation_compare_shim(void* pCtx, int len_a, const void* a, int len_b,
                           const void* b) noexcept {
    auto* cb = static_cast<Database::CollationCallback*>(pCtx);
    if (cb == nullptr || !(*cb)) {
        return 0;
    }
    try {
        std::string_view sa(static_cast<const char*>(a),
                            static_cast<std::size_t>(len_a));
        std::string_view sb(static_cast<const char*>(b),
                            static_cast<std::size_t>(len_b));
        return (*cb)(sa, sb);
    } catch (...) {
        return 0;
    }
}

void collation_destroy_shim(void* pCtx) noexcept {
    delete static_cast<Database::CollationCallback*>(pCtx);
}

} // anonymous namespace

void Database::set_wal_hook(WalHook cb) {
    if (!cb) {
        sqlite3_wal_hook(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->wal_hook = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.wal_hook = std::move(cb);
    sqlite3_wal_hook(db_holder_.get(), &HookState::wal_hook_shim, &state);
}

void Database::set_update_hook(UpdateHook cb) {
    if (!cb) {
        sqlite3_update_hook(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->update_hook = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.update_hook = std::move(cb);
    sqlite3_update_hook(db_holder_.get(), &HookState::update_hook_shim, &state);
}

void Database::set_commit_hook(CommitHook cb) {
    if (!cb) {
        sqlite3_commit_hook(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->commit_hook = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.commit_hook = std::move(cb);
    sqlite3_commit_hook(db_holder_.get(), &HookState::commit_hook_shim, &state);
}

void Database::set_rollback_hook(RollbackHook cb) {
    if (!cb) {
        sqlite3_rollback_hook(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->rollback_hook = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.rollback_hook = std::move(cb);
    sqlite3_rollback_hook(db_holder_.get(), &HookState::rollback_hook_shim,
                          &state);
}

void Database::set_preupdate_hook(PreupdateHook cb) {
    if (!cb) {
        sqlite3_preupdate_hook(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->preupdate_hook = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.preupdate_hook = std::move(cb);
    sqlite3_preupdate_hook(db_holder_.get(), &HookState::preupdate_hook_shim,
                           &state);
}

void Database::set_trace_hook(TraceFlags mask, TraceHook cb) {
    if (!cb || mask.empty()) {
        int rc = sqlite3_trace_v2(db_holder_.get(), 0, nullptr, nullptr);
        if (hooks_) {
            hooks_->trace_hook = nullptr;
        }
        throw_on_error(rc, db_holder_.get());
        return;
    }
    auto& state = ensure_hooks();
    state.trace_hook = std::move(cb);
    int rc = sqlite3_trace_v2(db_holder_.get(), mask.raw(),
                              &HookState::trace_hook_shim, &state);
    throw_on_error(rc, db_holder_.get());
}

void Database::set_progress_handler(int n_ops, ProgressHandler cb) {
    if (!cb || n_ops <= 0) {
        sqlite3_progress_handler(db_holder_.get(), 0, nullptr, nullptr);
        if (hooks_) {
            hooks_->progress_handler = nullptr;
        }
        return;
    }
    auto& state = ensure_hooks();
    state.progress_handler = std::move(cb);
    sqlite3_progress_handler(db_holder_.get(), n_ops,
                             &HookState::progress_handler_shim, &state);
}

void Database::set_busy_handler(BusyHandler cb) {
    if (!cb) {
        int rc = sqlite3_busy_handler(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->busy_handler = nullptr;
        }
        throw_on_error(rc, db_holder_.get());
        return;
    }
    auto& state = ensure_hooks();
    state.busy_handler = std::move(cb);
    int rc = sqlite3_busy_handler(db_holder_.get(),
                                  &HookState::busy_handler_shim, &state);
    throw_on_error(rc, db_holder_.get());
}

void Database::set_autovacuum_pages(AutovacuumPagesHook cb) {
    if (!cb) {
        int rc = sqlite3_autovacuum_pages(db_holder_.get(), nullptr, nullptr,
                                          nullptr);
        throw_on_error(rc, db_holder_.get());
        return;
    }
    auto* ctx = new AutovacuumPagesHook(std::move(cb));
    int rc = sqlite3_autovacuum_pages(db_holder_.get(), &autovacuum_pages_shim,
                                      ctx, &autovacuum_pages_destroy);
    if (rc != SQLITE_OK) {
        // sqlite3_autovacuum_pages does NOT call xDestroy on failure.
        delete ctx;
        throw_sqlite_error(rc, db_holder_.get());
    }
}

void Database::unlock_notify(UnlockNotifyHook cb) {
    if (!cb) {
        int rc = sqlite3_unlock_notify(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->unlock_notify = nullptr;
        }
        throw_on_error(rc, db_holder_.get());
        return;
    }
    auto& state = ensure_hooks();
    state.unlock_notify = std::move(cb);
    int rc = sqlite3_unlock_notify(db_holder_.get(),
                                   &HookState::unlock_notify_shim, &state);
    throw_on_error(rc, db_holder_.get());
}

void Database::setlk_timeout(std::chrono::milliseconds timeout, int flags) {
    int rc = sqlite3_setlk_timeout(db_holder_.get(),
                                   static_cast<int>(timeout.count()), flags);
    throw_on_error(rc, db_holder_.get());
}

// ============================================================================
// Collations & Extensions
// ============================================================================

void Database::create_collation(std::string_view name, CollationCallback cb) {
    std::string name_str(name);
    if (!cb) {
        drop_collation(name);
        return;
    }
    auto* ctx = new CollationCallback(std::move(cb));
    int rc = sqlite3_create_collation_v2(
        db_holder_.get(), name_str.c_str(), SQLITE_UTF8, ctx,
        &collation_compare_shim, &collation_destroy_shim);
    if (rc != SQLITE_OK) {
        // Note: SQLite calls xDestroy on error in sqlite3_create_collation_v2,
        // except when db is NULL/misuse; only throw after checking.
        throw_sqlite_error(rc, db_holder_.get());
    }
}

void Database::drop_collation(std::string_view name) {
    std::string name_str(name);
    int rc =
        sqlite3_create_collation_v2(db_holder_.get(), name_str.c_str(),
                                    SQLITE_UTF8, nullptr, nullptr, nullptr);
    throw_on_error(rc, db_holder_.get());
}

void Database::set_collation_needed(CollationNeededCallback cb) {
    if (!cb) {
        int rc = sqlite3_collation_needed(db_holder_.get(), nullptr, nullptr);
        if (hooks_) {
            hooks_->collation_needed = nullptr;
        }
        throw_on_error(rc, db_holder_.get());
        return;
    }
    auto& state = ensure_hooks();
    state.collation_needed = std::move(cb);
    int rc = sqlite3_collation_needed(db_holder_.get(), &state,
                                      &HookState::collation_needed_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::enable_load_extension(bool enable) {
    int rc = sqlite3_enable_load_extension(db_holder_.get(), enable ? 1 : 0);
    throw_on_error(rc, db_holder_.get());
}

void Database::load_extension(std::string_view path,
                              std::string_view entry_point) {
    std::string path_str(path);
    std::string proc_str(entry_point);
    const char* z_proc = entry_point.empty() ? nullptr : proc_str.c_str();
    char* err_msg = nullptr;
    int rc = sqlite3_load_extension(db_holder_.get(), path_str.c_str(), z_proc,
                                    &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg(err_msg ? err_msg : sqlite3_errmsg(db_holder_.get()));
        sqlite3_free(err_msg);
        throw_on_error(rc, msg.c_str());
    }
}

// ============================================================================
// Configuration, Limits & Status
// ============================================================================

bool Database::db_config(DbConfig op, bool enable) {
    int out = 0;
    int rc = sqlite3_db_config(db_holder_.get(), static_cast<int>(op),
                               enable ? 1 : 0, &out);
    throw_on_error(rc, db_holder_.get());
    return out != 0;
}

bool Database::db_config_get(DbConfig op) const {
    int out = 0;
    int rc = sqlite3_db_config(const_cast<sqlite3*>(db_holder_.get()),
                               static_cast<int>(op), -1, &out);
    throw_on_error(rc, db_holder_.get());
    return out != 0;
}

void Database::set_main_db_name(std::string_view name) {
    auto& state = ensure_hooks();
    // Allocate a new string first, register it, then keep it alive for the
    // lifetime of the connection (SQLite does not copy the pointer).
    std::string next_name(name);
    state.main_db_name = std::move(next_name);
    int rc = sqlite3_db_config(db_holder_.get(), SQLITE_DBCONFIG_MAINDBNAME,
                               state.main_db_name.c_str());
    throw_on_error(rc, db_holder_.get());
}

void Database::set_lookaside(void* buf, int slot_size, int slot_count) {
    int rc = sqlite3_db_config(db_holder_.get(), SQLITE_DBCONFIG_LOOKASIDE, buf,
                               slot_size, slot_count);
    throw_on_error(rc, db_holder_.get());
}

int Database::set_fp_digits(int digits) {
    int out = 0;
    int rc = sqlite3_db_config(db_holder_.get(), SQLITE_DBCONFIG_FP_DIGITS,
                               digits, &out);
    throw_on_error(rc, db_holder_.get());
    return out;
}

int Database::fp_digits() const {
    int out = 0;
    int rc = sqlite3_db_config(const_cast<sqlite3*>(db_holder_.get()),
                               SQLITE_DBCONFIG_FP_DIGITS, 0, &out);
    throw_on_error(rc, db_holder_.get());
    return out;
}

int Database::limit(LimitCategory id, int new_val) {
    return sqlite3_limit(db_holder_.get(), static_cast<int>(id), new_val);
}

DbStatusResult Database::db_status(DbStatusOp op, bool reset_highwater) const {
    sqlite3_int64 cur = 0;
    sqlite3_int64 hiwtr = 0;
    int rc = sqlite3_db_status64(const_cast<sqlite3*>(db_holder_.get()),
                                 static_cast<int>(op), &cur, &hiwtr,
                                 reset_highwater ? 1 : 0);
    throw_on_error(rc, db_holder_.get());
    return DbStatusResult{static_cast<int64_t>(cur),
                          static_cast<int64_t>(hiwtr)};
}

// ============================================================================
// Utility & Introspection
// ============================================================================

int64_t Database::last_insert_rowid() const noexcept {
    return sqlite3_last_insert_rowid(db_holder_.get());
}

void Database::set_last_insert_rowid(int64_t rowid) noexcept {
    sqlite3_set_last_insert_rowid(db_holder_.get(),
                                  static_cast<sqlite3_int64>(rowid));
}

int Database::changes() const noexcept {
    return sqlite3_changes(db_holder_.get());
}

int64_t Database::changes64() const noexcept {
    return static_cast<int64_t>(sqlite3_changes64(db_holder_.get()));
}

int64_t Database::total_changes() const noexcept {
    return sqlite3_total_changes(db_holder_.get());
}

int64_t Database::total_changes64() const noexcept {
    return static_cast<int64_t>(sqlite3_total_changes64(db_holder_.get()));
}

int Database::errcode() const noexcept {
    return sqlite3_errcode(db_holder_.get());
}

int Database::extended_errcode() const noexcept {
    return sqlite3_extended_errcode(db_holder_.get());
}

std::string_view Database::errmsg() const noexcept {
    const char* msg = sqlite3_errmsg(db_holder_.get());
    return msg ? std::string_view(msg) : std::string_view{};
}

int Database::error_offset() const noexcept {
    return sqlite3_error_offset(db_holder_.get());
}

int Database::system_errno() const noexcept {
    return sqlite3_system_errno(db_holder_.get());
}

void Database::set_extended_result_codes(bool enable) {
    int rc = sqlite3_extended_result_codes(db_holder_.get(), enable ? 1 : 0);
    throw_on_error(rc, db_holder_.get());
}

int Database::set_errmsg(int errcode, std::string_view msg) {
    std::string msg_str(msg);
    const char* z_msg = msg.empty() ? nullptr : msg_str.c_str();
    return sqlite3_set_errmsg(db_holder_.get(), errcode, z_msg);
}

void Database::interrupt() {
    sqlite3_interrupt(db_holder_.get());
}

bool Database::is_interrupted() const noexcept {
    return sqlite3_is_interrupted(const_cast<sqlite3*>(db_holder_.get())) != 0;
}

void Database::set_busy_timeout(std::chrono::milliseconds timeout) {
    throw_on_error(sqlite3_busy_timeout(db_holder_.get(),
                                        static_cast<int>(timeout.count())),
                   db_holder_.get());
}

bool Database::is_autocommit() const noexcept {
    return sqlite3_get_autocommit(db_holder_.get()) != 0;
}

TxnState Database::txn_state(std::string_view schema) const noexcept {
    std::string schema_str(schema);
    const char* z_schema = schema.empty() ? nullptr : schema_str.c_str();
    int st =
        sqlite3_txn_state(const_cast<sqlite3*>(db_holder_.get()), z_schema);
    switch (st) {
    case SQLITE_TXN_READ:
        return TxnState::Read;
    case SQLITE_TXN_WRITE:
        return TxnState::Write;
    default:
        return TxnState::None;
    }
}

DbReadOnlyState Database::db_readonly(std::string_view schema) const noexcept {
    std::string schema_str(schema);
    int const st = sqlite3_db_readonly(const_cast<sqlite3*>(db_holder_.get()),
                                       schema_str.c_str());
    if (st == 1) {
        return DbReadOnlyState::ReadOnly;
    }
    if (st == 0) {
        return DbReadOnlyState::ReadWrite;
    }
    return DbReadOnlyState::NoSuchSchema;
}

bool Database::is_readonly(std::string_view schema) const noexcept {
    return db_readonly(schema) == DbReadOnlyState::ReadOnly;
}

std::string_view Database::db_filename(std::string_view db_name) const {
    std::string name(db_name);
    const char* fname = sqlite3_db_filename(db_holder_.get(), name.c_str());
    return fname ? std::string_view(fname) : std::string_view{};
}

std::optional<std::string_view> Database::db_name(int index) const noexcept {
    const char* name =
        sqlite3_db_name(const_cast<sqlite3*>(db_holder_.get()), index);
    if (name == nullptr) {
        return std::nullopt;
    }
    return std::string_view(name);
}

std::vector<std::string> Database::db_names() const {
    std::vector<std::string> result;
    for (int i = 0;; ++i) {
        const char* name =
            sqlite3_db_name(const_cast<sqlite3*>(db_holder_.get()), i);
        if (name == nullptr) {
            break;
        }
        result.emplace_back(name);
    }
    return result;
}

void Database::db_cacheflush() {
    int rc = sqlite3_db_cacheflush(db_holder_.get());
    throw_on_error(rc, db_holder_.get());
}

void Database::db_release_memory() {
    int rc = sqlite3_db_release_memory(db_holder_.get());
    throw_on_error(rc, db_holder_.get());
}

sqlite3_stmt* Database::next_stmt(sqlite3_stmt* prev) const noexcept {
    return sqlite3_next_stmt(const_cast<sqlite3*>(db_holder_.get()), prev);
}

std::vector<sqlite3_stmt*> Database::active_statements() const {
    std::vector<sqlite3_stmt*> stmts;
    sqlite3_stmt* cur = nullptr;
    while ((cur = next_stmt(cur)) != nullptr) {
        stmts.push_back(cur);
    }
    return stmts;
}

ColumnMetadata Database::table_column_metadata(std::string_view table,
                                               std::string_view column,
                                               std::string_view db_name) const {
    std::string db_str(db_name);
    std::string tbl_str(table);
    std::string col_str(column);
    const char* z_db = db_name.empty() ? nullptr : db_str.c_str();
    const char* data_type = nullptr;
    const char* coll_seq = nullptr;
    int not_null = 0;
    int primary_key = 0;
    int auto_inc = 0;
    int rc = sqlite3_table_column_metadata(
        const_cast<sqlite3*>(db_holder_.get()), z_db, tbl_str.c_str(),
        col_str.c_str(), &data_type, &coll_seq, &not_null, &primary_key,
        &auto_inc);
    throw_on_error(rc, db_holder_.get());
    ColumnMetadata meta;
    if (data_type != nullptr) {
        meta.data_type = data_type;
    }
    if (coll_seq != nullptr) {
        meta.collation_sequence = coll_seq;
    }
    meta.not_null = not_null != 0;
    meta.primary_key = primary_key != 0;
    meta.auto_increment = auto_inc != 0;
    return meta;
}

bool Database::table_exists(std::string_view table,
                            std::string_view db_name) const {
    std::string db_str(db_name);
    std::string tbl_str(table);
    const char* z_db = db_name.empty() ? nullptr : db_str.c_str();
    int rc = sqlite3_table_column_metadata(
        const_cast<sqlite3*>(db_holder_.get()), z_db, tbl_str.c_str(), nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    return rc == SQLITE_OK;
}

void Database::set_clientdata(std::string_view name, void* data,
                              void (*destructor)(void*)) {
    std::string name_str(name);
    int rc = sqlite3_set_clientdata(db_holder_.get(), name_str.c_str(), data,
                                    destructor);
    throw_on_error(rc, db_holder_.get());
}

void* Database::get_clientdata(std::string_view name) const noexcept {
    std::string name_str(name);
    return sqlite3_get_clientdata(const_cast<sqlite3*>(db_holder_.get()),
                                  name_str.c_str());
}

int Database::file_control(std::string_view schema, int op, void* arg) {
    std::string schema_str(schema);
    const char* z_schema = schema.empty() ? nullptr : schema_str.c_str();
    return sqlite3_file_control(db_holder_.get(), z_schema, op, arg);
}

void Database::overload_function(std::string_view name, int n_args) {
    std::string name_str(name);
    int rc =
        sqlite3_overload_function(db_holder_.get(), name_str.c_str(), n_args);
    throw_on_error(rc, db_holder_.get());
}

void Database::drop_modules() {
    int rc = sqlite3_drop_modules(db_holder_.get(), nullptr);
    throw_on_error(rc, db_holder_.get());
}

void Database::drop_modules(std::span<const std::string_view> keep) {
    if (keep.empty()) {
        drop_modules();
        return;
    }
    std::vector<std::string> storage;
    storage.reserve(keep.size());
    std::vector<const char*> ptrs;
    ptrs.reserve(keep.size() + 1);
    for (auto sv : keep) {
        storage.emplace_back(sv);
        ptrs.push_back(storage.back().c_str());
    }
    ptrs.push_back(nullptr);
    int rc = sqlite3_drop_modules(db_holder_.get(), ptrs.data());
    throw_on_error(rc, db_holder_.get());
}

// ============================================================================
// Custom SQL Functions
// ============================================================================

namespace {

/// C shim for scalar functions. Extracts the ScalarFunction from user_data,
/// converts args, and calls it.
void scalar_func_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto& fn = *static_cast<ScalarFunction*>(sqlite3_user_data(ctx));
    try {
        auto args = detail::convert_args(argc, argv);
        FunctionContext fctx(ctx);
        fn(fctx, std::span<const Value>(args));
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in SQL function", -1);
    }
}

/// xDestroy for function contexts allocated via new.
void func_destroy_shim(void* p) noexcept {
    delete static_cast<ScalarFunction*>(p);
}

/// C shim for aggregate step.
void aggregate_step_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto& factory =
        *static_cast<std::function<std::unique_ptr<AggregateFunction>()>*>(
            sqlite3_user_data(ctx));

    void* mem = sqlite3_aggregate_context(ctx, sizeof(AggregateFunction*));
    if (mem == nullptr) {
        return; // out of memory
    }
    auto* agg_ptr = static_cast<AggregateFunction**>(mem);

    try {
        if ((*agg_ptr) == nullptr) {
            *agg_ptr = factory().release();
        }
        auto args = detail::convert_args(argc, argv);
        FunctionContext fctx(ctx);
        (*agg_ptr)->step(fctx, std::span<const Value>(args));
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in aggregate step",
                             -1);
    }
}

/// C shim for aggregate final.
void aggregate_final_shim(sqlite3_context* ctx) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    std::unique_ptr<AggregateFunction> owner;
    if (mem != nullptr) {
        auto* agg_ptr = static_cast<AggregateFunction**>(mem);
        owner.reset(std::exchange(*agg_ptr, nullptr));
        if (!owner) {
            // xStep ran and failed before constructing the aggregate; do not
            // invoke final() or overwrite its error.
            return;
        }
    }

    try {
        if (!owner) {
            // Empty input set: xStep was never called, so invoke final() on a
            // fresh default-initialized aggregate instance.
            auto& factory = *static_cast<
                std::function<std::unique_ptr<AggregateFunction>()>*>(
                sqlite3_user_data(ctx));
            owner = factory();
        }
        if (owner) {
            FunctionContext fctx(ctx);
            owner->final(fctx);
        }
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in aggregate final",
                             -1);
    }
}

/// xDestroy for aggregate factory contexts.
void aggregate_destroy_shim(void* p) noexcept {
    delete static_cast<std::function<std::unique_ptr<AggregateFunction>()>*>(p);
}

/// C shim for window step (distinct from aggregate_step_shim to avoid
/// cross-casting unrelated std::function specializations).
void window_step_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto& factory =
        *static_cast<std::function<std::unique_ptr<WindowFunction>()>*>(
            sqlite3_user_data(ctx));

    void* mem = sqlite3_aggregate_context(ctx, sizeof(WindowFunction*));
    if (mem == nullptr) {
        return; // out of memory
    }
    auto* win_ptr = static_cast<WindowFunction**>(mem);

    try {
        if ((*win_ptr) == nullptr) {
            *win_ptr = factory().release();
        }
        auto args = detail::convert_args(argc, argv);
        FunctionContext fctx(ctx);
        (*win_ptr)->step(fctx, std::span<const Value>(args));
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in window step", -1);
    }
}

/// C shim for window final.
void window_final_shim(sqlite3_context* ctx) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    std::unique_ptr<WindowFunction> owner;
    if (mem != nullptr) {
        auto* win_ptr = static_cast<WindowFunction**>(mem);
        owner.reset(std::exchange(*win_ptr, nullptr));
        if (!owner) {
            return;
        }
    }

    try {
        if (!owner) {
            auto& factory =
                *static_cast<std::function<std::unique_ptr<WindowFunction>()>*>(
                    sqlite3_user_data(ctx));
            owner = factory();
        }
        if (owner) {
            FunctionContext fctx(ctx);
            owner->final(fctx);
        }
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in window final", -1);
    }
}

/// C shim for window value.
void window_value_shim(sqlite3_context* ctx) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    WindowFunction* win = nullptr;
    if (mem != nullptr) {
        win = *static_cast<WindowFunction**>(mem);
        if (win == nullptr) {
            return;
        }
    }

    try {
        std::unique_ptr<WindowFunction> transient;
        if (win == nullptr) {
            auto& factory =
                *static_cast<std::function<std::unique_ptr<WindowFunction>()>*>(
                    sqlite3_user_data(ctx));
            transient = factory();
            win = transient.get();
        }
        if (win != nullptr) {
            FunctionContext fctx(ctx);
            win->value(fctx);
        }
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in window value", -1);
    }
}

/// C shim for window inverse.
void window_inverse_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    if (mem == nullptr) {
        return;
    }
    auto* win_ptr = static_cast<WindowFunction**>(mem);
    if ((*win_ptr) == nullptr) {
        return;
    }

    try {
        auto args = detail::convert_args(argc, argv);
        FunctionContext fctx(ctx);
        (*win_ptr)->inverse(fctx, std::span<const Value>(args));
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in window inverse",
                             -1);
    }
}

/// xDestroy for window factory contexts.
void window_destroy_shim(void* p) noexcept {
    delete static_cast<std::function<std::unique_ptr<WindowFunction>()>*>(p);
}

/// C shim for raw scalar functions (non-owning ValueRef args).
void raw_scalar_func_shim(sqlite3_context* ctx, int argc,
                          sqlite3_value** argv) {
    auto& fn = *static_cast<RawScalarFunction*>(sqlite3_user_data(ctx));
    try {
        std::vector<ValueRef> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        FunctionContext fctx(ctx);
        fn(fctx, std::span<const ValueRef>(args));
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in raw SQL function",
                             -1);
    }
}

void raw_func_destroy_shim(void* p) noexcept {
    delete static_cast<RawScalarFunction*>(p);
}

} // anonymous namespace

void Database::create_function(std::string_view name, int n_args,
                               ScalarFunction fn, FunctionFlags flags) {
    auto* ctx = new ScalarFunction(std::move(fn));
    int rc = sqlite3_create_function_v2(
        db_holder_.get(), std::string(name).c_str(), n_args, flags.raw(), ctx,
        &scalar_func_shim, nullptr, nullptr, &func_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::create_raw_function(std::string_view name, int n_args,
                                   RawScalarFunction fn, FunctionFlags flags) {
    auto* ctx = new RawScalarFunction(std::move(fn));
    int rc = sqlite3_create_function_v2(
        db_holder_.get(), std::string(name).c_str(), n_args, flags.raw(), ctx,
        &raw_scalar_func_shim, nullptr, nullptr, &raw_func_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::drop_function(std::string_view name, int n_args,
                             FunctionFlags flags) {
    int rc = sqlite3_create_function_v2(
        db_holder_.get(), std::string(name).c_str(), n_args, flags.raw(),
        nullptr, nullptr, nullptr, nullptr, nullptr);
    throw_on_error(rc, db_holder_.get());
}

void Database::create_aggregate(
    std::string_view name, int n_args,
    std::function<std::unique_ptr<AggregateFunction>()> factory,
    FunctionFlags flags) {
    auto* ctx = new std::function<std::unique_ptr<AggregateFunction>()>(
        std::move(factory));
    int rc = sqlite3_create_function_v2(
        db_holder_.get(), std::string(name).c_str(), n_args, flags.raw(), ctx,
        nullptr, &aggregate_step_shim, &aggregate_final_shim,
        &aggregate_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::create_window_function(
    std::string_view name, int n_args,
    std::function<std::unique_ptr<WindowFunction>()> factory,
    FunctionFlags flags) {
    auto* ctx = new std::function<std::unique_ptr<WindowFunction>()>(
        std::move(factory));
    int rc = sqlite3_create_window_function(
        db_holder_.get(), std::string(name).c_str(), n_args, flags.raw(), ctx,
        &window_step_shim, &window_final_shim, &window_value_shim,
        &window_inverse_shim, &window_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

// Forward-declared — implemented in virtual_table.cpp
ModuleRegistration create_module_impl(
    sqlite3* db, std::string_view name,
    std::function<std::unique_ptr<VirtualTable>()> factory);

ModuleRegistration Database::create_module(
    std::string_view name,
    std::function<std::unique_ptr<VirtualTable>()> factory) {
    return create_module_impl(db_holder_.get(), name, std::move(factory));
}

} // namespace sqlitex
