#pragma once

#include "cloudsqlite/error.h"

#include <sqlite3.h>
#include "cloudsqlite/detail/session_api.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cloudsqlite {

class Value;

// ============================================================================
// ChangeOperation — type of change in a changeset row
// ============================================================================

enum class ChangeOperation {
    Insert = SQLITE_INSERT,
    Delete = SQLITE_DELETE,
    Update = SQLITE_UPDATE,
};

// ============================================================================
// ChangesetIterator — RAII wrapper for sqlite3_changeset_iter*
// ============================================================================

class ChangesetIterator {
public:
    ChangesetIterator() noexcept = default;

    ~ChangesetIterator();

    // Move-only
    ChangesetIterator(ChangesetIterator&& other) noexcept;
    ChangesetIterator& operator=(ChangesetIterator&& other) noexcept;
    ChangesetIterator(const ChangesetIterator&) = delete;
    ChangesetIterator& operator=(const ChangesetIterator&) = delete;

    explicit operator bool() const noexcept { return iter_ != nullptr; }

    // === Navigation ===

    /// Advance to the next change. Returns true if a change is available.
    bool next();

    // === Change Metadata ===

    ChangeOperation op() const;
    std::string table_name() const;
    int column_count() const;
    std::vector<bool> primary_key() const;
    bool is_indirect() const;

    // === Value Access ===

    /// Old value for column i (available for DELETE and UPDATE).
    std::optional<int64_t> old_int(int i) const;
    std::optional<double> old_double(int i) const;
    std::optional<std::string> old_text(int i) const;
    std::optional<std::vector<uint8_t>> old_blob(int i) const;
    bool old_is_null(int i) const;

    /// New value for column i (available for INSERT and UPDATE).
    std::optional<int64_t> new_int(int i) const;
    std::optional<double> new_double(int i) const;
    std::optional<std::string> new_text(int i) const;
    std::optional<std::vector<uint8_t>> new_blob(int i) const;
    bool new_is_null(int i) const;

    /// Conflicting value (only during conflict handler callbacks).
    std::optional<int64_t> conflict_int(int i) const;
    std::optional<double> conflict_double(int i) const;
    std::optional<std::string> conflict_text(int i) const;
    std::optional<std::vector<uint8_t>> conflict_blob(int i) const;

    /// Foreign key conflict count.
    int fk_conflict_count() const;

    // === Low-level access ===

    sqlite3_changeset_iter* handle() noexcept { return iter_; }

    /// Release ownership of the underlying iterator.
    /// Used internally for borrowed iterators in conflict callbacks.
    void release_ownership() noexcept { owned_ = false; }

    /// Create a non-owning iterator borrowed from a conflict handler.
    static ChangesetIterator borrow(sqlite3_changeset_iter* iter) noexcept {
        return ChangesetIterator(iter, false);
    }

private:
    friend class Changeset;
    friend class Patchset;

    /// Owned construction (from Changeset::begin()).
    explicit ChangesetIterator(sqlite3_changeset_iter* iter) noexcept;

    /// Internal: owned_ == false means borrowed.
    ChangesetIterator(sqlite3_changeset_iter* iter, bool own) noexcept
        : iter_(iter), owned_(own) {}

    sqlite3_changeset_iter* iter_ = nullptr;
    bool owned_ = false;

    // Helpers
    sqlite3_value* old_value_raw(int i) const;
    sqlite3_value* new_value_raw(int i) const;
    sqlite3_value* conflict_value_raw(int i) const;
};

} // namespace cloudsqlite
