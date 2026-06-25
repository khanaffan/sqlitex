#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"

#include "sqlitex/detail/session_api.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

class ChangesetIterator;
class Changegroup;
class Database;

// ============================================================================
// Conflict types and actions
// ============================================================================

enum class ConflictType {
    Data = SQLITE_CHANGESET_DATA,
    NotFound = SQLITE_CHANGESET_NOTFOUND,
    Conflict = SQLITE_CHANGESET_CONFLICT,
    Constraint = SQLITE_CHANGESET_CONSTRAINT,
    ForeignKey = SQLITE_CHANGESET_FOREIGN_KEY,
};

enum class ChangesetAction {
    Omit = SQLITE_CHANGESET_OMIT,       // 0
    Replace = SQLITE_CHANGESET_REPLACE, // 1
    Abort = SQLITE_CHANGESET_ABORT,     // 2
};

// ============================================================================
// Apply flags
// ============================================================================

enum class ApplyFlag : int {
    None = 0,
    NoSavepoint = SQLITE_CHANGESETAPPLY_NOSAVEPOINT,
    Invert = SQLITE_CHANGESETAPPLY_INVERT,
    IgnoreNoop = SQLITE_CHANGESETAPPLY_IGNORENOOP,
    FkNoAction = SQLITE_CHANGESETAPPLY_FKNOACTION,
};

using ApplyFlags = int; // bitmask of ApplyFlag

// ============================================================================
// Changeset — value type wrapping a binary changeset/patchset blob
// ============================================================================

class SQLITEX_EXPORT Changeset {
    public:
    Changeset() noexcept = default;

    /// Take ownership of a blob allocated by sqlite3_malloc.
    Changeset(void* data, int size) noexcept;

    /// Create a Changeset by copying external data.
    static Changeset copy_from(const void* data, int size);

    ~Changeset() = default;

    // Move-only
    Changeset(Changeset&& other) noexcept;
    Changeset& operator=(Changeset&& other) noexcept;
    Changeset(const Changeset&) = delete;
    Changeset& operator=(const Changeset&) = delete;

    explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

    int size() const noexcept {
        return size_;
    }
    const void* data() const noexcept {
        return data_.get();
    }
    bool empty() const noexcept {
        return size_ == 0 || data_ == nullptr;
    }

    // === Operations ===

    /// Create an iterator to traverse this changeset.
    /// @param flags Pass SQLITE_CHANGESETSTART_INVERT to invert during
    /// iteration
    [[nodiscard]] ChangesetIterator begin(int flags = 0) const;

    /// Concatenate two changesets into a new one.
    [[nodiscard]] Changeset concat(const Changeset& other) const;

    /// Invert this changeset (creates a "revert" changeset).
    [[nodiscard]] Changeset invert() const;

    // === Apply ===

    /// Apply changeset to a database with conflict handling.
    /// @param db              Target database
    /// @param conflict_handler Resolves conflicts; receives ConflictType +
    /// iterator
    /// @param filter          Optional per-table filter (table name → allow)
    void apply(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {}) const;

    /// Apply with additional flags and optional rebase output.
    void apply_v2(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        ApplyFlags flags = 0) const;

    /// Apply with per-change filter (v3 API).
    void apply_v3(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(const ChangesetIterator&)> per_change_filter = {},
        ApplyFlags flags = 0) const;

    // === Streaming Apply ===

    /// Stream-apply a changeset from an input callback.
    /// @param db               Target database
    /// @param input            Input callback providing changeset data
    /// @param conflict_handler Resolves conflicts
    /// @param table_filter     Optional per-table filter
    static void apply_stream(
        Database& db,
        StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {});

    /// Stream-apply with additional flags.
    static void apply_v2_stream(
        Database& db,
        StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        ApplyFlags flags = 0);

    // === Streaming Operations ===

    /// Stream-concatenate two changesets from input callbacks, producing
    /// output via an output callback.
    static void concat_stream(
        StreamInputFunc input_a,
        StreamInputFunc input_b,
        StreamOutputFunc output);

    /// Stream-invert a changeset from an input callback, producing output
    /// via an output callback.
    static void invert_stream(
        StreamInputFunc input,
        StreamOutputFunc output);

    private:
    struct Deleter {
        void operator()(void* p) const noexcept {
            sqlite3_free(p);
        }
    };

    std::unique_ptr<void, Deleter> data_{};
    int size_ = 0;

    friend class Session;
    friend class Changegroup;
};

// ============================================================================
// Patchset — distinct type for patchset blobs
// ============================================================================

class SQLITEX_EXPORT Patchset {
    public:
    Patchset() noexcept = default;

    /// Take ownership of a patchset blob.
    Patchset(void* data, int size) noexcept;

    ~Patchset() = default;

    // Move-only
    Patchset(Patchset&& other) noexcept;
    Patchset& operator=(Patchset&& other) noexcept;
    Patchset(const Patchset&) = delete;
    Patchset& operator=(const Patchset&) = delete;

    explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

    int size() const noexcept {
        return size_;
    }
    const void* data() const noexcept {
        return data_.get();
    }
    bool empty() const noexcept {
        return size_ == 0 || data_ == nullptr;
    }

    /// Create an iterator for this patchset.
    [[nodiscard]] ChangesetIterator begin(int flags = 0) const;

    /// Apply patchset to a database.
    void apply(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {}) const;

    /// Stream-apply a patchset from an input callback.
    static void apply_stream(
        Database& db,
        StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {});

    private:
    struct Deleter {
        void operator()(void* p) const noexcept {
            sqlite3_free(p);
        }
    };

    std::unique_ptr<void, Deleter> data_{};
    int size_ = 0;

    friend class Session;
    friend class Changegroup;
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
