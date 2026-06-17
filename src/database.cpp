#include "sqlitex/database.h"
#include "sqlitex/error.h"
#include "sqlitex/statement.h"
#include "sqlitex/virtual_table.h"

#include <sqlite3.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

Database::Database(std::string_view path, OpenFlags flags,
                   std::string_view vfs) {
    open(path, flags, vfs);
}

// unique_ptr handles close

Database::Database(Database&& other) noexcept
    : db_holder_(std::move(other.db_holder_)) {
}

Database& Database::operator=(Database&& other) noexcept {
    if (this != &other) {
        close();
        db_holder_ = std::move(other.db_holder_);
    }
    return *this;
}

void Database::open(std::string_view path, OpenFlags flags,
                    std::string_view vfs) {
    close();

    sqlite3* raw = nullptr;
    // string_view::data() is not null-terminated; copy to std::string first.
    std::string path_str(path);
    std::string vfs_str(vfs);
    const char* vfs_name = vfs.empty() ? nullptr : vfs_str.c_str();

    int rc = sqlite3_open_v2(path_str.c_str(), &raw, flags, vfs_name);
    if (rc != SQLITE_OK) {
        if (raw != nullptr) {
            // Capture the error message before closing the handle, then throw
            // via throw_on_error so the correct Error subclass is raised.
            std::string msg(sqlite3_errmsg(raw));
            sqlite3_close_v2(raw);
            throw_on_error(rc, msg.c_str());
        }
        throw_on_error(rc, (std::string("Cannot open database: ") +
                             std::string(path)).c_str());
    }

    db_holder_.reset(raw);
}

void Database::close() noexcept {
    db_holder_.reset();
}

// ============================================================================
// Query Execution
// ============================================================================

Statement Database::prepare(std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    std::string sql_str(sql);

    int rc = sqlite3_prepare_v2(db_holder_.get(), sql_str.c_str(),
                                static_cast<int>(sql_str.size()), &stmt, &tail);
    throw_on_error(rc, db_holder_.get());

    return {stmt, this};
}

void Database::execute(std::string_view sql) {
    std::string sql_str(sql);
    char* err_msg = nullptr;

    int rc = sqlite3_exec(db_holder_.get(), sql_str.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        // err_msg may carry more detail than sqlite3_errmsg; use it when present.
        std::string msg(err_msg ? err_msg : sqlite3_errmsg(db_holder_.get()));
        sqlite3_free(err_msg);
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
            return (*ec->cb)(ncols, values, names) ? 0 : 1;
        } catch (...) {
            *ec->exc = std::current_exception();
            return 1; // signal SQLite to abort
        }
    };

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_holder_.get(), sql_str.c_str(), exec_cb, &ctx, &err_msg);
    sqlite3_free(err_msg);

    // Re-throw any C++ exception raised inside the callback.
    if (pending_exc) {
        std::rethrow_exception(pending_exc);
    }

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

void Database::begin_transaction() {
    execute("BEGIN");
}

void Database::commit() {
    execute("COMMIT");
}

void Database::rollback() {
    execute("ROLLBACK");
}

// ============================================================================
// Utility
// ============================================================================

int64_t Database::last_insert_rowid() const noexcept {
    return sqlite3_last_insert_rowid(db_holder_.get());
}

int Database::changes() const noexcept {
    return sqlite3_changes(db_holder_.get());
}

int64_t Database::total_changes() const noexcept {
    return sqlite3_total_changes(db_holder_.get());
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

void Database::interrupt() {
    sqlite3_interrupt(db_holder_.get());
}

void Database::set_busy_timeout(int ms) {
    throw_on_error(sqlite3_busy_timeout(db_holder_.get(), ms), db_holder_.get());
}

bool Database::is_autocommit() const noexcept {
    return sqlite3_get_autocommit(db_holder_.get()) != 0;
}

std::string_view Database::db_filename(std::string_view db_name) const {
    std::string name(db_name);
    const char* fname = sqlite3_db_filename(db_holder_.get(), name.c_str());
    return fname ? std::string_view(fname) : std::string_view{};
}

// ============================================================================
// Custom SQL Functions
// ============================================================================

namespace {

/// C shim for scalar functions.  Extracts the ScalarFunction from user_data,
/// converts args, and calls it.
void scalar_func_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    auto& fn = *static_cast<ScalarFunction*>(sqlite3_user_data(ctx));
    try {
        auto args = detail::convert_args(argc, argv);
        FunctionContext fctx(ctx);
        fn(fctx, args);
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

    // Retrieve or create the per-group AggregateFunction instance.
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
        (*agg_ptr)->step(fctx, args);
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
    if (mem == nullptr) {
        return;
    }
    auto* agg_ptr = static_cast<AggregateFunction**>(mem);
    if ((*agg_ptr) == nullptr) {
        return;
    }

    try {
        FunctionContext fctx(ctx);
        (*agg_ptr)->final(fctx);
        delete *agg_ptr;
        *agg_ptr = nullptr;
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

/// C shim for window step (same as aggregate step).
void window_step_shim(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    aggregate_step_shim(ctx, argc, argv);
}

/// C shim for window final.
void window_final_shim(sqlite3_context* ctx) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    if (mem == nullptr) {
        return;
    }
    auto* win_ptr = static_cast<WindowFunction**>(mem);
    if ((*win_ptr) == nullptr) {
        return;
    }

    try {
        FunctionContext fctx(ctx);
        (*win_ptr)->final(fctx);
        delete *win_ptr;
        *win_ptr = nullptr;
    } catch (const std::exception& e) {
        sqlite3_result_error(ctx, e.what(), -1);
    } catch (...) {
        sqlite3_result_error(ctx, "Unknown C++ exception in window final", -1);
    }
}

/// C shim for window value.
void window_value_shim(sqlite3_context* ctx) {
    void* mem = sqlite3_aggregate_context(ctx, 0);
    if (mem == nullptr) {
        return;
    }
    auto* win_ptr = static_cast<WindowFunction**>(mem);
    if ((*win_ptr) == nullptr) {
        return;
    }

    try {
        FunctionContext fctx(ctx);
        (*win_ptr)->value(fctx);
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
        (*win_ptr)->inverse(fctx, args);
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

} // anonymous namespace

void Database::create_function(std::string_view name, int n_args,
                               ScalarFunction fn, int flags) {
    auto* ctx = new ScalarFunction(std::move(fn));
    int rc = sqlite3_create_function_v2(db_holder_.get(), std::string(name).c_str(), n_args,
                                        flags, ctx, &scalar_func_shim,
                                        nullptr, // xStep
                                        nullptr, // xFinal
                                        &func_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::create_aggregate(
    std::string_view name, int n_args,
    std::function<std::unique_ptr<AggregateFunction>()> factory,
    int flags) {
    auto* ctx = new std::function<std::unique_ptr<AggregateFunction>()>(
        std::move(factory));
    int rc = sqlite3_create_function_v2(
        db_holder_.get(), std::string(name).c_str(), n_args, flags, ctx,
        nullptr, // xFunc
        &aggregate_step_shim, &aggregate_final_shim, &aggregate_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

void Database::create_window_function(
    std::string_view name, int n_args,
    std::function<std::unique_ptr<WindowFunction>()> factory,
    int flags) {
    auto* ctx = new std::function<std::unique_ptr<WindowFunction>()>(
        std::move(factory));
    int rc = sqlite3_create_window_function(
        db_holder_.get(), std::string(name).c_str(), n_args, flags, ctx, &window_step_shim,
        &window_final_shim, &window_value_shim, &window_inverse_shim,
        &window_destroy_shim);
    throw_on_error(rc, db_holder_.get());
}

// Forward-declared — implemented in virtual_table.cpp
ModuleRegistration create_module_impl(
    sqlite3* db, std::string_view name,
    std::function<std::unique_ptr<VirtualTable>()> factory, int flags);

ModuleRegistration Database::create_module(
    std::string_view name,
    std::function<std::unique_ptr<VirtualTable>()> factory, int flags) {
    return create_module_impl(db_holder_.get(), name, std::move(factory), flags);
}

} // namespace sqlitex
