#include "sqlitex/changeset_iterator.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <cstring>
#include <string>
#include <utility>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

ChangesetIterator::ChangesetIterator(sqlite3_changeset_iter* iter) noexcept
    : iter_(iter), owned_(true) {
}

ChangesetIterator::~ChangesetIterator() {
    if (owned_ && (iter_ != nullptr)) {
        sqlite3changeset_finalize(iter_);
    }
}

ChangesetIterator::ChangesetIterator(ChangesetIterator&& other) noexcept
    : iter_(std::exchange(other.iter_, nullptr)),
      owned_(std::exchange(other.owned_, false)),
      stream_input_(std::move(other.stream_input_)) {
}

ChangesetIterator& ChangesetIterator::operator=(
    ChangesetIterator&& other) noexcept {
    if (this != &other) {
        if (owned_ && (iter_ != nullptr)) {
            sqlite3changeset_finalize(iter_);
        }
        iter_ = std::exchange(other.iter_, nullptr);
        owned_ = std::exchange(other.owned_, false);
        stream_input_ = std::move(other.stream_input_);
    }
    return *this;
}

// ============================================================================
// Navigation
// ============================================================================

bool ChangesetIterator::next() {
    int const rc = sqlite3changeset_next(iter_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_on_error(rc, "sqlite3changeset_next failed");
    return false;
}

// ============================================================================
// Change Metadata
// ============================================================================

ChangeOperation ChangesetIterator::op() const {
    const char* zTab = nullptr;
    int nCol = 0;
    int op = 0;
    int bIndirect = 0;
    int const rc = sqlite3changeset_op(iter_, &zTab, &nCol, &op, &bIndirect);
    throw_on_error(rc, "sqlite3changeset_op failed");
    return static_cast<ChangeOperation>(op);
}

std::string ChangesetIterator::table_name() const {
    const char* zTab = nullptr;
    int nCol = 0;
    int op = 0;
    int bIndirect = 0;
    int const rc = sqlite3changeset_op(iter_, &zTab, &nCol, &op, &bIndirect);
    throw_on_error(rc, "sqlite3changeset_op failed");
    return zTab ? std::string(zTab) : std::string{};
}

int ChangesetIterator::column_count() const {
    const char* zTab = nullptr;
    int nCol = 0;
    int op = 0;
    int bIndirect = 0;
    int const rc = sqlite3changeset_op(iter_, &zTab, &nCol, &op, &bIndirect);
    throw_on_error(rc, "sqlite3changeset_op failed");
    return nCol;
}

std::vector<bool> ChangesetIterator::primary_key() const {
    unsigned char* abPK = nullptr;
    int nCol = 0;
    int const rc = sqlite3changeset_pk(iter_, &abPK, &nCol);
    throw_on_error(rc, "sqlite3changeset_pk failed");

    std::vector<bool> result;
    result.reserve(static_cast<std::size_t>(nCol));
    for (int i = 0; i < nCol; ++i) {
        result.push_back(abPK[i] != 0);
    }
    return result;
}

bool ChangesetIterator::is_indirect() const {
    const char* zTab = nullptr;
    int nCol = 0;
    int op = 0;
    int bIndirect = 0;
    int const rc = sqlite3changeset_op(iter_, &zTab, &nCol, &op, &bIndirect);
    throw_on_error(rc, "sqlite3changeset_op failed");
    return bIndirect != 0;
}

// ============================================================================
// Value access helpers
// ============================================================================

sqlite3_value* ChangesetIterator::old_value_raw(int i) const {
    sqlite3_value* val = nullptr;
    int const rc = sqlite3changeset_old(iter_, i, &val);
    // Non-OK means the value is not available (e.g. old on INSERT).
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    return val;
}

sqlite3_value* ChangesetIterator::new_value_raw(int i) const {
    sqlite3_value* val = nullptr;
    int const rc = sqlite3changeset_new(iter_, i, &val);
    // Non-OK means the value is not available (e.g. new on DELETE).
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    return val;
}

sqlite3_value* ChangesetIterator::conflict_value_raw(int i) const {
    sqlite3_value* val = nullptr;
    int const rc = sqlite3changeset_conflict(iter_, i, &val);
    throw_on_error(rc, "sqlite3changeset_conflict failed");
    return val;
}

// ============================================================================
// Old values
// ============================================================================

bool ChangesetIterator::old_is_null(int i) const {
    sqlite3_value* val = old_value_raw(i);
    if (val == nullptr) {
        return true; // not available
    }
    return sqlite3_value_type(val) == SQLITE_NULL;
}

std::optional<int64_t> ChangesetIterator::old_int(int i) const {
    if (old_is_null(i))
        return std::nullopt;
    return sqlite3_value_int64(old_value_raw(i));
}

std::optional<double> ChangesetIterator::old_double(int i) const {
    if (old_is_null(i))
        return std::nullopt;
    return sqlite3_value_double(old_value_raw(i));
}

std::optional<std::string> ChangesetIterator::old_text(int i) const {
    if (old_is_null(i))
        return std::nullopt;
    sqlite3_value* val = old_value_raw(i);
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(val));
    int const n = sqlite3_value_bytes(val);
    return std::string(text, static_cast<std::size_t>(n));
}

std::optional<std::vector<uint8_t>> ChangesetIterator::old_blob(int i) const {
    if (old_is_null(i))
        return std::nullopt;
    sqlite3_value* val = old_value_raw(i);
    const auto* data = static_cast<const uint8_t*>(sqlite3_value_blob(val));
    int n = sqlite3_value_bytes(val);
    return std::vector<uint8_t>(data, data + n);
}

// ============================================================================
// New values
// ============================================================================

bool ChangesetIterator::new_is_null(int i) const {
    sqlite3_value* val = new_value_raw(i);
    if (val == nullptr) {
        return true; // not available
    }
    return sqlite3_value_type(val) == SQLITE_NULL;
}

std::optional<int64_t> ChangesetIterator::new_int(int i) const {
    if (new_is_null(i))
        return std::nullopt;
    return sqlite3_value_int64(new_value_raw(i));
}

std::optional<double> ChangesetIterator::new_double(int i) const {
    if (new_is_null(i))
        return std::nullopt;
    return sqlite3_value_double(new_value_raw(i));
}

std::optional<std::string> ChangesetIterator::new_text(int i) const {
    if (new_is_null(i))
        return std::nullopt;
    sqlite3_value* val = new_value_raw(i);
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(val));
    int const n = sqlite3_value_bytes(val);
    return std::string(text, static_cast<std::size_t>(n));
}

std::optional<std::vector<uint8_t>> ChangesetIterator::new_blob(int i) const {
    if (new_is_null(i))
        return std::nullopt;
    sqlite3_value* val = new_value_raw(i);
    const auto* data = static_cast<const uint8_t*>(sqlite3_value_blob(val));
    int n = sqlite3_value_bytes(val);
    return std::vector<uint8_t>(data, data + n);
}

// ============================================================================
// Conflict values
// ============================================================================

std::optional<int64_t> ChangesetIterator::conflict_int(int i) const {
    sqlite3_value* val = conflict_value_raw(i);
    if (sqlite3_value_type(val) == SQLITE_NULL)
        return std::nullopt;
    return sqlite3_value_int64(val);
}

std::optional<double> ChangesetIterator::conflict_double(int i) const {
    sqlite3_value* val = conflict_value_raw(i);
    if (sqlite3_value_type(val) == SQLITE_NULL)
        return std::nullopt;
    return sqlite3_value_double(val);
}

std::optional<std::string> ChangesetIterator::conflict_text(int i) const {
    sqlite3_value* val = conflict_value_raw(i);
    if (sqlite3_value_type(val) == SQLITE_NULL)
        return std::nullopt;
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(val));
    int const n = sqlite3_value_bytes(val);
    return std::string(text, static_cast<std::size_t>(n));
}

std::optional<std::vector<uint8_t>> ChangesetIterator::conflict_blob(
    int i) const {
    sqlite3_value* val = conflict_value_raw(i);
    if (sqlite3_value_type(val) == SQLITE_NULL)
        return std::nullopt;
    const auto* data = static_cast<const uint8_t*>(sqlite3_value_blob(val));
    int n = sqlite3_value_bytes(val);
    return std::vector<uint8_t>(data, data + n);
}

int ChangesetIterator::fk_conflict_count() const {
    int n = 0;
    int const rc = sqlite3changeset_fk_conflicts(iter_, &n);
    throw_on_error(rc, "sqlite3changeset_fk_conflicts failed");
    return n;
}

// ============================================================================
// Streaming factory
// ============================================================================

ChangesetIterator ChangesetIterator::from_stream(
    StreamInputFunc input, int flags) {
    auto input_ptr = std::make_unique<StreamInputFunc>(std::move(input));
    sqlite3_changeset_iter* iter = nullptr;

    int rc = sqlite3changeset_start_v2_strm(
        &iter,
        detail::stream_input_shim,
        static_cast<void*>(input_ptr.get()),
        flags);
    throw_on_error(rc, "sqlite3changeset_start_v2_strm failed");

    ChangesetIterator result(iter);
    result.stream_input_ = std::move(input_ptr);
    return result;
}

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
