#include "sqlitex/virtual_table.h"
#include "sqlitex/function.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// IndexInfo implementation
// ============================================================================

void IndexInfo::set_idx_str(const char* str) {
    if (info_->needToFreeIdxStr && info_->idxStr != nullptr) {
        sqlite3_free(info_->idxStr);
    }
    if (str == nullptr) {
        info_->idxStr = nullptr;
        info_->needToFreeIdxStr = 0;
        return;
    }
    info_->idxStr = sqlite3_mprintf("%s", str);
    info_->needToFreeIdxStr = 1;
}

void IndexInfo::set_idx_str(std::string_view str) {
    if (info_->needToFreeIdxStr && info_->idxStr != nullptr) {
        sqlite3_free(info_->idxStr);
    }
    info_->idxStr = sqlite3_mprintf("%.*s", static_cast<int>(str.size()),
                                    str.empty() ? "" : str.data());
    info_->needToFreeIdxStr = 1;
}

int IndexInfo::num_constraints() const noexcept {
    return info_->nConstraint;
}

IndexInfo::Constraint IndexInfo::constraint(int i) const noexcept {
    auto& c = info_->aConstraint[i];
    return {c.iColumn, static_cast<Op>(c.op), c.usable != 0};
}

int IndexInfo::num_order_by() const noexcept {
    return info_->nOrderBy;
}

IndexInfo::OrderBy IndexInfo::order_by(int i) const noexcept {
    auto& o = info_->aOrderBy[i];
    return {o.iColumn, o.desc != 0};
}

bool IndexInfo::col_used(int column) const noexcept {
    return (info_->colUsed & (static_cast<sqlite3_uint64>(1) << column)) != 0;
}

void IndexInfo::set_omit(int constraint_index, bool omit) {
    info_->aConstraintUsage[constraint_index].omit = omit ? 1 : 0;
}

void IndexInfo::set_argv_index(int constraint_index, int argv_index) {
    info_->aConstraintUsage[constraint_index].argvIndex = argv_index + 1;
}

void IndexInfo::set_idx_num(int num) noexcept {
    info_->idxNum = num;
}

void IndexInfo::set_order_by_consumed(bool consumed) {
    info_->orderByConsumed = consumed ? 1 : 0;
}

void IndexInfo::set_estimated_cost(double cost) {
    info_->estimatedCost = cost;
}

void IndexInfo::set_estimated_rows(int64_t rows) {
    info_->estimatedRows = rows;
}

void IndexInfo::set_idx_flags(int flags) noexcept {
    info_->idxFlags = flags;
}

std::string_view IndexInfo::collation(int constraint_index) const noexcept {
    const char* name = sqlite3_vtab_collation(info_, constraint_index);
    return name ? std::string_view(name) : std::string_view{};
}

int IndexInfo::distinct() const noexcept {
    return sqlite3_vtab_distinct(info_);
}

bool IndexInfo::set_in(int constraint_index, int handle_mode) noexcept {
    return sqlite3_vtab_in(info_, constraint_index, handle_mode) != 0;
}

std::optional<Value> IndexInfo::rhs_value(int constraint_index) const {
    sqlite3_value* val = nullptr;
    int rc = sqlite3_vtab_rhs_value(info_, constraint_index, &val);
    if (rc == SQLITE_OK && val != nullptr) {
        return Value::from_sqlite3_value(val);
    }
    return std::nullopt;
}

// ============================================================================
// VtabInIterator & Cursor::filter_raw & VirtualTable static helpers
// ============================================================================

VtabInIterator::VtabInIterator(sqlite3_value* in_list) noexcept
    : in_list_(in_list), done_(in_list == nullptr) {
}

bool VtabInIterator::next() {
    if (done_) {
        return false;
    }
    int rc = 0;
    if (!started_) {
        started_ = true;
        rc = sqlite3_vtab_in_first(in_list_, &current_);
    } else {
        rc = sqlite3_vtab_in_next(in_list_, &current_);
    }
    if (rc == SQLITE_OK) {
        return true;
    }
    done_ = true;
    current_ = nullptr;
    if (rc != SQLITE_DONE) {
        throw_sqlite_error(rc, nullptr);
    }
    return false;
}

std::vector<Value> VtabInIterator::collect(sqlite3_value* in_list) {
    std::vector<Value> out;
    VtabInIterator it(in_list);
    while (it.next()) {
        out.push_back(it.current_value());
    }
    return out;
}

int Cursor::filter_raw(int idx_num, const char* idx_str,
                       const std::vector<ValueRef>& argv) {
    std::vector<Value> owned;
    owned.reserve(argv.size());
    for (const auto& ref : argv) {
        owned.push_back(ref.to_value());
    }
    return filter(idx_num, idx_str, owned);
}

int VirtualTable::declare_vtab(sqlite3* db, std::string_view schema) {
    std::string schema_str(schema);
    return sqlite3_declare_vtab(db, schema_str.c_str());
}

int VirtualTable::vtab_config(sqlite3* db, VtabConfig op, int val) {
    if (op == VtabConfig::ConstraintSupport) {
        return sqlite3_vtab_config(db, static_cast<int>(op), val);
    }
    return sqlite3_vtab_config(db, static_cast<int>(op));
}

int VirtualTable::vtab_on_conflict(sqlite3* db) noexcept {
    return sqlite3_vtab_on_conflict(db);
}

// ============================================================================
// ModuleRegistration
// ============================================================================

ModuleRegistration::ModuleRegistration(sqlite3* db, std::string name,
                                       std::shared_ptr<void> module_ctx)
    : db_(db), name_(std::move(name)), module_ctx_(std::move(module_ctx)) {
}

ModuleRegistration::~ModuleRegistration() {
    if (db_ && !name_.empty()) {
        sqlite3_create_module_v2(db_, name_.c_str(), nullptr, nullptr, nullptr);
    }
}

ModuleRegistration::ModuleRegistration(ModuleRegistration&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)),
      name_(std::move(other.name_)),
      module_ctx_(std::move(other.module_ctx_)) {
}

ModuleRegistration& ModuleRegistration::operator=(
    ModuleRegistration&& other) noexcept {
    if (this != &other) {
        if (db_ && !name_.empty()) {
            sqlite3_create_module_v2(db_, name_.c_str(), nullptr, nullptr,
                                     nullptr);
        }
        db_ = std::exchange(other.db_, nullptr);
        name_ = std::move(other.name_);
        module_ctx_ = std::move(other.module_ctx_);
    }
    return *this;
}

// ============================================================================
// Internal: embedding structs and C shim trampolines
// ============================================================================

namespace detail {

/// VTab embedding: sqlite3_vtab must be the first member (per SQLite ABI).
struct VTabImpl {
    sqlite3_vtab base{};
    std::unique_ptr<VirtualTable> impl{};
};

/// Cursor embedding: sqlite3_vtab_cursor must be the first member.
struct CursorImpl {
    sqlite3_vtab_cursor base{};
    std::unique_ptr<Cursor> impl{};
};

/// Module context stored as pAux / user_data.
struct ModuleCtx {
    sqlite3_module module;
    std::function<std::unique_ptr<VirtualTable>()> factory;
};

// Helpers for extracting C++ objects from C structs.
// Since sqlite3_vtab is the first member of VTabImpl (and similarly
// sqlite3_vtab_cursor for CursorImpl), the pointer values are identical.
// We use reinterpret_cast because there is no inheritance relationship.
static inline VTabImpl* from_vtab(sqlite3_vtab* v) {
    return reinterpret_cast<VTabImpl*>(v);
}
static inline CursorImpl* from_cursor(sqlite3_vtab_cursor* c) {
    return reinterpret_cast<CursorImpl*>(c);
}
static inline ModuleCtx* from_aux(void* p) {
    // pAux is a shared_ptr<ModuleCtx>* — dereference to get the ModuleCtx*
    auto* sp = static_cast<std::shared_ptr<ModuleCtx>*>(p);
    return sp->get();
}

} // namespace detail
} // namespace sqlitex

// ============================================================================
// C trampolines for sqlite3_module
// These are plain static functions with internal linkage at file scope,
// outside any C++ namespace.  Placing extern "C" inside a namespace is
// non-conforming per [dcl.link]/4; since SQLite uses function pointers
// (not name lookup), extern "C" linkage is not required.
// ============================================================================

static int xCreate_shim(sqlite3* db, void* pAux, int argc,
                        const char* const* argv, sqlite3_vtab** pp_vtab,
                        char** pzErr) {
    try {
        auto* mod = sqlitex::detail::from_aux(pAux);
        auto impl = mod->factory();
        if (!impl) {
            if (pzErr != nullptr) {
                *pzErr = sqlite3_mprintf("factory returned null");
            }
            return SQLITE_ERROR;
        }

        int rc = impl->create(db, argc, argv);
        if (rc != SQLITE_OK) {
            return rc;
        }

        auto vtab = std::make_unique<sqlitex::detail::VTabImpl>();
        vtab->impl = std::move(impl);
        *pp_vtab = &vtab->base;
        vtab.release(); // ownership → SQLite (freed in xDisconnect_shim)
        return SQLITE_OK;
    } catch (const std::exception& e) {
        if (pzErr != nullptr) {
            *pzErr = sqlite3_mprintf("%s", e.what());
        }
        return SQLITE_ERROR;
    } catch (...) {
        if (pzErr != nullptr) {
            *pzErr = sqlite3_mprintf("unknown C++ exception in xCreate");
        }
        return SQLITE_ERROR;
    }
}

static int xConnect_shim(sqlite3* db, void* pAux, int argc,
                         const char* const* argv, sqlite3_vtab** pp_vtab,
                         char** pzErr) {
    try {
        auto* mod = sqlitex::detail::from_aux(pAux);
        auto impl = mod->factory();
        if (!impl) {
            if (pzErr != nullptr) {
                *pzErr = sqlite3_mprintf("factory returned null");
            }
            return SQLITE_ERROR;
        }

        int rc = impl->connect(db, argc, argv);
        if (rc != SQLITE_OK) {
            return rc;
        }

        auto vtab = std::make_unique<sqlitex::detail::VTabImpl>();
        vtab->impl = std::move(impl);
        *pp_vtab = &vtab->base;
        vtab.release();
        return SQLITE_OK;
    } catch (const std::exception& e) {
        if (pzErr != nullptr) {
            *pzErr = sqlite3_mprintf("%s", e.what());
        }
        return SQLITE_ERROR;
    } catch (...) {
        if (pzErr != nullptr) {
            *pzErr = sqlite3_mprintf("unknown C++ exception in xConnect");
        }
        return SQLITE_ERROR;
    }
}

static int xBestIndex_shim(sqlite3_vtab* vtab, sqlite3_index_info* idx) {
    try {
        sqlitex::IndexInfo info(idx);
        return sqlitex::detail::from_vtab(vtab)->impl->best_index(info);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xDisconnect_shim(sqlite3_vtab* vtab) {
    try {
        auto* v = sqlitex::detail::from_vtab(vtab);
        int rc = v->impl->disconnect();
        v->impl.reset();
        delete v; // allocated in xCreate_shim / xConnect_shim
        return rc;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xDestroy_shim(sqlite3_vtab* vtab) {
    try {
        int rc = sqlitex::detail::from_vtab(vtab)->impl->destroy();
        // xDisconnect follows to free the struct
        return rc;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xOpen_shim(sqlite3_vtab* vtab, sqlite3_vtab_cursor** pp_cursor) {
    try {
        auto cursor = std::make_unique<sqlitex::detail::CursorImpl>();
        int rc = sqlitex::detail::from_vtab(vtab)->impl->open(cursor->impl);
        if (rc != SQLITE_OK) {
            return rc;
        }
        if (!cursor->impl) {
            return SQLITE_ERROR;
        }
        *pp_cursor = &cursor->base;
        cursor.release(); // ownership → SQLite (freed in xClose_shim)
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xClose_shim(sqlite3_vtab_cursor* cur) {
    try {
        auto* c = sqlitex::detail::from_cursor(cur);
        c->impl.reset();
        delete c;
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xFilter_shim(sqlite3_vtab_cursor* cur, int idx_num,
                        const char* idx_str, int argc, sqlite3_value** argv) {
    try {
        std::vector<sqlitex::ValueRef> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
        return sqlitex::detail::from_cursor(cur)->impl->filter_raw(
            idx_num, idx_str, args);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xNext_shim(sqlite3_vtab_cursor* cur) {
    try {
        return sqlitex::detail::from_cursor(cur)->impl->next();
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xEof_shim(sqlite3_vtab_cursor* cur) {
    try {
        return sqlitex::detail::from_cursor(cur)->impl->eof() ? 1 : 0;
    } catch (...) {
        return 1; // error → act as if end-of-data
    }
}

static int xColumn_shim(sqlite3_vtab_cursor* cur, sqlite3_context* ctx,
                        int col) {
    try {
        sqlitex::FunctionContext fctx(ctx);
        fctx.set_result(sqlitex::detail::from_cursor(cur)->impl->column(col));
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xRowid_shim(sqlite3_vtab_cursor* cur, sqlite3_int64* p_rowid) {
    try {
        *p_rowid = sqlitex::detail::from_cursor(cur)->impl->rowid();
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xUpdate_shim(sqlite3_vtab* vtab, int argc, sqlite3_value** argv,
                        sqlite3_int64* p_rowid) {
    try {
        // Convert argv to typed Value vector so VirtualTable::update() does
        // not need to handle raw sqlite3_value**.
        // argv[0] = old rowid (or NULL for INSERT)
        // argv[1] = new rowid / NULL
        // argv[2..] = new column values
        std::vector<sqlitex::Value> args;
        args.reserve(static_cast<std::size_t>(argc));
        for (int i = 0; i < argc; ++i) {
            args.push_back(argv[i] ? sqlitex::Value::from_sqlite3_value(argv[i])
                                   : sqlitex::Value{});
        }
        return sqlitex::detail::from_vtab(vtab)->impl->update(
            argc, args, reinterpret_cast<int64_t*>(p_rowid));
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xBegin_shim(sqlite3_vtab* vtab) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->begin_txn();
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xSync_shim(sqlite3_vtab* vtab) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->sync();
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xCommit_shim(sqlite3_vtab* vtab) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->commit_txn();
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xRollback_shim(sqlite3_vtab* vtab) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->rollback_txn();
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xFindFunction_shim(sqlite3_vtab* vtab, int n_arg, const char* z_name,
                              void (**px_func)(sqlite3_context*, int,
                                               sqlite3_value**),
                              void** pp_arg) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->find_function(
            n_arg, z_name, px_func, pp_arg);
    } catch (...) {
        return 0;
    }
}

static int xRename_shim(sqlite3_vtab* vtab, const char* z_new) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->rename(z_new);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xSavepoint_shim(sqlite3_vtab* vtab, int savepoint) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->savepoint(0, savepoint);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xRelease_shim(sqlite3_vtab* vtab, int savepoint) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->savepoint(1, savepoint);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

static int xRollbackTo_shim(sqlite3_vtab* vtab, int savepoint) {
    try {
        return sqlitex::detail::from_vtab(vtab)->impl->savepoint(2, savepoint);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

namespace sqlitex {
namespace detail {

/// Build the sqlite3_module struct with all C shim pointers.
/// Must be kept alive for the lifetime of the module registration.
static sqlite3_module make_module() {
    sqlite3_module m = {};
    m.iVersion = 3;
    m.xCreate = xCreate_shim;
    m.xConnect = xConnect_shim;
    m.xBestIndex = xBestIndex_shim;
    m.xDisconnect = xDisconnect_shim;
    m.xDestroy = xDestroy_shim;
    m.xOpen = xOpen_shim;
    m.xClose = xClose_shim;
    m.xFilter = xFilter_shim;
    m.xNext = xNext_shim;
    m.xEof = xEof_shim;
    m.xColumn = xColumn_shim;
    m.xRowid = xRowid_shim;
    m.xUpdate = xUpdate_shim;
    m.xBegin = xBegin_shim;
    m.xSync = xSync_shim;
    m.xCommit = xCommit_shim;
    m.xRollback = xRollback_shim;
    m.xFindFunction = xFindFunction_shim;
    m.xRename = xRename_shim;
    // xSavepoint / xRelease / xRollbackTo require iVersion >= 2.
    m.xSavepoint = xSavepoint_shim;
    m.xRelease = xRelease_shim;
    m.xRollbackTo = xRollbackTo_shim;
    return m;
}

} // namespace detail

// ============================================================================
// Database::create_module (implemented here to access detail::ModuleCtx)
// ============================================================================

ModuleRegistration create_module_impl(
    sqlite3* db, std::string_view name,
    std::function<std::unique_ptr<VirtualTable>()> factory, int flags) {
    // sqlite3_create_module_v2 does not accept a flags parameter.
    // SQLITE_VTAB_INNOCUOUS must be set on the xConnect method instead.
    (void)flags;
    auto mod_ctx = std::make_shared<detail::ModuleCtx>();
    mod_ctx->module = detail::make_module();
    mod_ctx->factory = std::move(factory);

    // Pass a copy of the shared_ptr to SQLite as pClientData.
    // The xDestroy callback deletes this copy when the module is unregistered.
    auto* client_data = new std::shared_ptr<detail::ModuleCtx>(mod_ctx);

    std::string name_str(name);
    int rc = sqlite3_create_module_v2(
        db, name_str.c_str(), &mod_ctx->module, client_data, [](void* p) {
            delete static_cast<std::shared_ptr<detail::ModuleCtx>*>(p);
        });
    throw_on_error(rc, db);

    return ModuleRegistration(db, std::move(name_str), std::move(mod_ctx));
}

} // namespace sqlitex
