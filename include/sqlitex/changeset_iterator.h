#pragma once

#include "sqlitex_export.h"

#include "sqlitex/changeset.h"
#include "sqlitex/error.h"
#include "sqlitex/value.h"

#include "sqlitex/detail/session_api.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

// ============================================================================
// ChangesetIterator — RAII wrapper & C++20 input_range for
// sqlite3_changeset_iter*
// ============================================================================

class SQLITEX_EXPORT ChangesetIterator
    : public std::ranges::view_interface<ChangesetIterator> {
    public:
    class Iterator {
        public:
        using iterator_concept = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = ChangesetIterator;
        using difference_type = std::ptrdiff_t;

        Iterator() noexcept = default;
        explicit Iterator(ChangesetIterator* owner) noexcept : owner_(owner) {
        }

        [[nodiscard]] const ChangesetIterator& operator*() const noexcept {
            return *owner_;
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
            return owner_ == nullptr;
        }

        private:
        ChangesetIterator* owner_ = nullptr;
    };

    ChangesetIterator() noexcept = default;

    ~ChangesetIterator();

    // Move-only
    ChangesetIterator(ChangesetIterator&& other) noexcept;
    ChangesetIterator& operator=(ChangesetIterator&& other) noexcept;
    ChangesetIterator(const ChangesetIterator&) = delete;
    ChangesetIterator& operator=(const ChangesetIterator&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return iter_ != nullptr;
    }

    // === Navigation & Range Interface ===

    /// Advance to the next change. Returns true if a change is available.
    bool next();

    [[nodiscard]] Iterator begin() {
        if (iter_ == nullptr || !next()) {
            return Iterator{};
        }
        return Iterator{this};
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

    // === Change Metadata ===

    [[nodiscard]] ChangeOperation op() const;
    [[nodiscard]] std::string table_name() const;
    [[nodiscard]] int column_count() const;
    [[nodiscard]] std::vector<bool> primary_key() const;
    [[nodiscard]] bool is_indirect() const;

    // === Value Access ===

    /// Old value for column i (available for DELETE and UPDATE).
    [[nodiscard]] std::optional<int64_t> old_int(int i) const;
    [[nodiscard]] std::optional<double> old_double(int i) const;
    [[nodiscard]] std::optional<std::string> old_text(int i) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> old_blob(int i) const;
    [[nodiscard]] std::optional<Value> old_value(int i) const;
    [[nodiscard]] bool old_is_null(int i) const;

    /// New value for column i (available for INSERT and UPDATE).
    [[nodiscard]] std::optional<int64_t> new_int(int i) const;
    [[nodiscard]] std::optional<double> new_double(int i) const;
    [[nodiscard]] std::optional<std::string> new_text(int i) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> new_blob(int i) const;
    [[nodiscard]] std::optional<Value> new_value(int i) const;
    [[nodiscard]] bool new_is_null(int i) const;

    /// Conflicting value (only during conflict handler callbacks).
    [[nodiscard]] std::optional<int64_t> conflict_int(int i) const;
    [[nodiscard]] std::optional<double> conflict_double(int i) const;
    [[nodiscard]] std::optional<std::string> conflict_text(int i) const;
    [[nodiscard]] std::optional<std::vector<std::byte>> conflict_blob(
        int i) const;
    [[nodiscard]] Value conflict_value(int i) const;

    /// Foreign key conflict count.
    [[nodiscard]] int fk_conflict_count() const;

    // === Low-level access ===

    [[nodiscard]] sqlite3_changeset_iter* handle() noexcept {
        return iter_;
    }
    [[nodiscard]] sqlite3_changeset_iter* handle() const noexcept {
        return iter_;
    }

    /// Release ownership of the underlying iterator.
    /// Used internally for borrowed iterators in conflict callbacks.
    void release_ownership() noexcept {
        owned_ = false;
    }

    /// Create a non-owning iterator borrowed from a conflict handler.
    [[nodiscard]] static ChangesetIterator borrow(
        sqlite3_changeset_iter* iter) noexcept {
        return {iter, false};
    }

    /// Create an iterator that reads changeset data from a streaming input
    /// callback.
    [[nodiscard]] static ChangesetIterator from_stream(
        StreamInputFunc input, ChangesetStartFlags flags = {});

    private:
    friend class Changeset;
    friend class Patchset;

    /// Owned construction (from Changeset::changes()).
    explicit ChangesetIterator(sqlite3_changeset_iter* iter) noexcept;

    /// Internal: owned_ == false means borrowed.
    ChangesetIterator(sqlite3_changeset_iter* iter, bool own) noexcept
        : iter_(iter), owned_(own) {
    }

    sqlite3_changeset_iter* iter_ = nullptr;
    bool owned_ = false;

    /// Kept alive for streaming iterators so the input callback remains
    /// valid for the iterator's lifetime.
    std::unique_ptr<StreamInputFunc> stream_input_{};

    // Helpers
    sqlite3_value* old_value_raw(int i) const;
    sqlite3_value* new_value_raw(int i) const;
    sqlite3_value* conflict_value_raw(int i) const;
};

static_assert(std::ranges::input_range<ChangesetIterator>);
static_assert(std::ranges::view<ChangesetIterator>);

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
