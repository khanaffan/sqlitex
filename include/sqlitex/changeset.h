#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"

#include "sqlitex/detail/blob.h"
#include "sqlitex/detail/session_api.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

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
// Changeset start and apply flags
// ============================================================================

enum class ChangesetStartFlag : int {
    None = 0,
    Invert = SQLITE_CHANGESETSTART_INVERT,
};

template <>
struct EnableBitmaskOperators<ChangesetStartFlag> : std::true_type {};

using ChangesetStartFlags = Flags<ChangesetStartFlag>;

enum class ApplyFlag : int {
    None = 0,
    NoSavepoint = SQLITE_CHANGESETAPPLY_NOSAVEPOINT,
    Invert = SQLITE_CHANGESETAPPLY_INVERT,
    IgnoreNoop = SQLITE_CHANGESETAPPLY_IGNORENOOP,
    FkNoAction = SQLITE_CHANGESETAPPLY_FKNOACTION,
    NoUpdateLoop = SQLITE_CHANGESETAPPLY_NOUPDATELOOP,
};

template <>
struct EnableBitmaskOperators<ApplyFlag> : std::true_type {};

using ApplyFlags = Flags<ApplyFlag>;

// ============================================================================
// Changeset — value type wrapping a binary changeset blob
// ============================================================================

class SQLITEX_EXPORT Changeset {
    public:
    Changeset() noexcept = default;

    /// Take ownership of a blob allocated by sqlite3_malloc.
    Changeset(void* data, int size) noexcept;

    /// Create a Changeset by copying bytes from a span or byte range.
    [[nodiscard]] static Changeset copy_from(std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    [[nodiscard]] static Changeset copy_from(const R& data) {
        return copy_from(to_const_bytes(data));
    }

    ~Changeset() = default;

    // Move-only
    Changeset(Changeset&& other) noexcept;
    Changeset& operator=(Changeset&& other) noexcept;
    Changeset(const Changeset&) = delete;
    Changeset& operator=(const Changeset&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(size_);
    }
    [[nodiscard]] const void* data() const noexcept {
        return data_.get();
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        if (data_ == nullptr || size_ <= 0) {
            return {};
        }
        return {static_cast<const std::byte*>(data_.get()),
                static_cast<std::size_t>(size_)};
    }
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0 || data_ == nullptr;
    }

    // === Operations ===

    /// Create an iterator / input_range to traverse this changeset.
    /// Pass ChangesetStartFlag::Invert to invert during iteration.
    [[nodiscard]] ChangesetIterator changes(
        ChangesetStartFlags flags = {}) const;

    /// Concatenate two changesets into a new one.
    [[nodiscard]] Changeset concat(const Changeset& other) const;

    /// Invert this changeset (creates a "revert" changeset).
    [[nodiscard]] Changeset invert() const;

    // === Apply ===

    /// Apply changeset to a database with conflict handling.
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
        ApplyFlags flags = {},
        std::vector<std::byte>* rebase_out = nullptr) const;

    /// Apply with per-change filter (v3 API) and optional rebase output.
    void apply_v3(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(const ChangesetIterator&)> per_change_filter = {},
        ApplyFlags flags = {},
        std::vector<std::byte>* rebase_out = nullptr) const;

    // === Streaming Apply ===

    /// Stream-apply a changeset from an input callback.
    static void apply_stream(
        Database& db, StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {});

    /// Stream-apply with additional flags and optional rebase output.
    static void apply_v2_stream(
        Database& db, StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        ApplyFlags flags = {}, std::vector<std::byte>* rebase_out = nullptr);

    /// Stream-apply with per-change filter (v3 streaming API) and optional
    /// rebase output.
    static void apply_v3_stream(
        Database& db, StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(const ChangesetIterator&)> per_change_filter = {},
        ApplyFlags flags = {}, std::vector<std::byte>* rebase_out = nullptr);

    // === Streaming Operations ===

    /// Stream-concatenate two changesets from input callbacks, producing
    /// output via an output callback.
    static void concat_stream(StreamInputFunc input_a, StreamInputFunc input_b,
                              StreamOutputFunc output);

    /// Stream-invert a changeset from an input callback, producing output
    /// via an output callback.
    static void invert_stream(StreamInputFunc input, StreamOutputFunc output);

    private:
    struct Deleter {
        void operator()(void* p) const noexcept {
            detail::free_sqlite_memory(p);
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

    [[nodiscard]] explicit operator bool() const noexcept {
        return data_ != nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(size_);
    }
    [[nodiscard]] const void* data() const noexcept {
        return data_.get();
    }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        if (data_ == nullptr || size_ <= 0) {
            return {};
        }
        return {static_cast<const std::byte*>(data_.get()),
                static_cast<std::size_t>(size_)};
    }
    [[nodiscard]] bool empty() const noexcept {
        return size_ == 0 || data_ == nullptr;
    }

    /// Create an iterator / input_range for this patchset.
    [[nodiscard]] ChangesetIterator changes(
        ChangesetStartFlags flags = {}) const;

    /// Apply patchset to a database.
    void apply(
        Database& db,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {}) const;

    /// Stream-apply a patchset from an input callback.
    static void apply_stream(
        Database& db, StreamInputFunc input,
        std::function<ChangesetAction(ConflictType, const ChangesetIterator&)>
            conflict_handler,
        std::function<bool(std::string_view)> table_filter = {});

    private:
    struct Deleter {
        void operator()(void* p) const noexcept {
            detail::free_sqlite_memory(p);
        }
    };

    std::unique_ptr<void, Deleter> data_{};
    int size_ = 0;

    friend class Session;
    friend class Changegroup;
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
