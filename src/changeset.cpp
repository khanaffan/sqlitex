#include "cloudsqlite/changeset.h"
#include "cloudsqlite/changeset_iterator.h"
#include "cloudsqlite/database.h"
#include "cloudsqlite/error.h"

#include <sqlite3.h>

#include <cstring>
#include <string>
#include <utility>

namespace cloudsqlite {

// ============================================================================
// Changeset — construction / destruction
// ============================================================================

Changeset::Changeset(void* data, int size) noexcept
    : data_(data), size_(size) {}

Changeset Changeset::copy_from(const void* src, int size) {
    void* buf = sqlite3_malloc(static_cast<sqlite3_uint64>(size));
    if (!buf && size > 0) throw std::bad_alloc();
    if (buf && size > 0) {
        std::memcpy(buf, src, static_cast<std::size_t>(size));
    }
    return Changeset(buf, size);
}

Changeset::~Changeset() = default;

Changeset::Changeset(Changeset&& other) noexcept
    : data_(std::exchange(other.data_, nullptr))
    , size_(std::exchange(other.size_, 0)) {}

Changeset& Changeset::operator=(Changeset&& other) noexcept {
    if (this != &other) {
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

// ============================================================================
// Changeset — operations
// ============================================================================

ChangesetIterator Changeset::begin(int flags) const {
    sqlite3_changeset_iter* iter = nullptr;

    int rc = sqlite3changeset_start_v2(&iter, size_,
                                        const_cast<void*>(data_.get()), flags);
    throw_on_error(rc, "sqlite3changeset_start_v2 failed");

    return ChangesetIterator(iter);
}

Changeset Changeset::concat(const Changeset& other) const {
    void* out = nullptr;
    int n_out = 0;

    int rc = sqlite3changeset_concat(
        size_, const_cast<void*>(data_.get()),
        other.size_, const_cast<void*>(other.data_.get()),
        &n_out, &out);
    throw_on_error(rc, "sqlite3changeset_concat failed");

    return Changeset(out, n_out);
}

Changeset Changeset::invert() const {
    void* out = nullptr;
    int n_out = 0;

    int rc = sqlite3changeset_invert(
        size_, const_cast<void*>(data_.get()),
        &n_out, &out);
    throw_on_error(rc, "sqlite3changeset_invert failed");

    return Changeset(out, n_out);
}

// ============================================================================
// Changeset — apply helpers (C callback shims)
// ============================================================================

namespace {

using ConflictHandler = std::function<ChangesetAction(ConflictType,
    const ChangesetIterator&)>;
using TableFilter = std::function<bool(std::string_view)>;
using ChangeFilter = std::function<bool(const ChangesetIterator&)>;

// Combined context for apply_v2: xFilter and xConflict share the same pCtx.
struct ApplyCtxV2 {
    ConflictHandler* conflict;
    TableFilter* table_filter;
};

int conflict_shim_v2(void* ctx, int eConflict, sqlite3_changeset_iter* pIter) {
    auto* ac = static_cast<ApplyCtxV2*>(ctx);
    ChangesetIterator iter = ChangesetIterator::borrow(pIter);
    ChangesetAction action = (*ac->conflict)(
        static_cast<ConflictType>(eConflict), iter);
    iter.release_ownership();
    switch (action) {
    case ChangesetAction::Omit:    return SQLITE_CHANGESET_OMIT;
    case ChangesetAction::Replace: return SQLITE_CHANGESET_REPLACE;
    case ChangesetAction::Abort:   return SQLITE_CHANGESET_ABORT;
    }
    return SQLITE_CHANGESET_OMIT;
}

int table_filter_shim_v2(void* ctx, const char* tbl_name) {
    auto* ac = static_cast<ApplyCtxV2*>(ctx);
    if (!ac->table_filter || !(*ac->table_filter)) return 1;
    return (*ac->table_filter)(std::string_view(tbl_name ? tbl_name : "")) ? 1 : 0;
}

// Combined context for apply_v3: per-change xFilter and xConflict share pCtx.
struct ApplyCtxV3 {
    ConflictHandler* conflict;
    ChangeFilter* change_filter;
};

int change_filter_shim_v3(void* ctx, sqlite3_changeset_iter* pIter) {
    auto* ac = static_cast<ApplyCtxV3*>(ctx);
    if (!ac->change_filter || !(*ac->change_filter)) return 1;
    ChangesetIterator iter = ChangesetIterator::borrow(pIter);
    bool result = (*ac->change_filter)(iter);
    iter.release_ownership();
    return result ? 1 : 0;
}

int conflict_shim_v3(void* ctx, int eConflict, sqlite3_changeset_iter* pIter) {
    auto* ac = static_cast<ApplyCtxV3*>(ctx);
    ChangesetIterator iter = ChangesetIterator::borrow(pIter);
    ChangesetAction action = (*ac->conflict)(
        static_cast<ConflictType>(eConflict), iter);
    iter.release_ownership();
    switch (action) {
    case ChangesetAction::Omit:    return SQLITE_CHANGESET_OMIT;
    case ChangesetAction::Replace: return SQLITE_CHANGESET_REPLACE;
    case ChangesetAction::Abort:   return SQLITE_CHANGESET_ABORT;
    }
    return SQLITE_CHANGESET_OMIT;
}

} // anonymous namespace

// ============================================================================
// Changeset — apply
// ============================================================================

void Changeset::apply(
    Database& db,
    std::function<ChangesetAction(ConflictType, const ChangesetIterator&)> conflict_handler,
    std::function<bool(std::string_view)> table_filter) const {

    TableFilter tf = table_filter ? std::move(table_filter) : nullptr;
    ApplyCtxV2 ctx{&conflict_handler, tf ? &tf : nullptr};

    int rc = sqlite3changeset_apply_v2(
        db.handle(),
        size_, const_cast<void*>(data_.get()),
        tf ? table_filter_shim_v2 : nullptr,
        conflict_shim_v2, &ctx,
        nullptr, nullptr, 0);

    if (rc != SQLITE_OK) {
        throw ChangesetApplyError(
            ErrorCode::Error,
            std::string("changeset apply failed: ") + std::string(db.errmsg()));
    }
}

void Changeset::apply_v2(
    Database& db,
    std::function<ChangesetAction(ConflictType, const ChangesetIterator&)> conflict_handler,
    ApplyFlags flags) const {

    ApplyCtxV2 ctx{&conflict_handler, nullptr};

    int rc = sqlite3changeset_apply_v2(
        db.handle(),
        size_, const_cast<void*>(data_.get()),
        nullptr, conflict_shim_v2, &ctx,
        nullptr, nullptr,
        static_cast<int>(flags));

    if (rc != SQLITE_OK) {
        throw ChangesetApplyError(
            ErrorCode::Error,
            std::string("changeset apply_v2 failed: ") + std::string(db.errmsg()));
    }
}

void Changeset::apply_v3(
    Database& db,
    std::function<ChangesetAction(ConflictType, const ChangesetIterator&)> conflict_handler,
    std::function<bool(const ChangesetIterator&)> per_change_filter,
    ApplyFlags flags) const {

    ChangeFilter cf = per_change_filter ? std::move(per_change_filter) : nullptr;
    ApplyCtxV3 ctx{&conflict_handler, cf ? &cf : nullptr};

    int rc = sqlite3changeset_apply_v3(
        db.handle(),
        size_, const_cast<void*>(data_.get()),
        cf ? change_filter_shim_v3 : nullptr,
        conflict_shim_v3, &ctx,
        nullptr, nullptr,
        static_cast<int>(flags));

    if (rc != SQLITE_OK) {
        throw ChangesetApplyError(
            ErrorCode::Error,
            std::string("changeset apply_v3 failed: ") + std::string(db.errmsg()));
    }
}

// ============================================================================
// Patchset
// ============================================================================

Patchset::Patchset(void* data, int size) noexcept
    : data_(data), size_(size) {}

Patchset::~Patchset() = default;

Patchset::Patchset(Patchset&& other) noexcept
    : data_(std::exchange(other.data_, nullptr))
    , size_(std::exchange(other.size_, 0)) {}

Patchset& Patchset::operator=(Patchset&& other) noexcept {
    if (this != &other) {
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

ChangesetIterator Patchset::begin(int flags) const {
    sqlite3_changeset_iter* iter = nullptr;

    int rc = sqlite3changeset_start_v2(&iter, size_,
                                        const_cast<void*>(data_.get()), flags);
    throw_on_error(rc, "sqlite3changeset_start_v2 failed");

    return ChangesetIterator(iter);
}

void Patchset::apply(
    Database& db,
    std::function<ChangesetAction(ConflictType, const ChangesetIterator&)> conflict_handler,
    std::function<bool(std::string_view)> table_filter) const {

    TableFilter tf = table_filter ? std::move(table_filter) : nullptr;
    ApplyCtxV2 ctx{&conflict_handler, tf ? &tf : nullptr};

    int rc = sqlite3changeset_apply_v2(
        db.handle(),
        size_, const_cast<void*>(data_.get()),
        tf ? table_filter_shim_v2 : nullptr,
        conflict_shim_v2, &ctx,
        nullptr, nullptr, 0);

    if (rc != SQLITE_OK) {
        throw ChangesetApplyError(
            ErrorCode::Error,
            std::string("patchset apply failed: ") + std::string(db.errmsg()));
    }
}

} // namespace cloudsqlite
