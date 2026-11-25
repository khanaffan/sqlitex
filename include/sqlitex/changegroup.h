#pragma once

#include "sqlitex_export.h"

#include "sqlitex/changeset.h"
#include "sqlitex/changeset_iterator.h"
#include "sqlitex/concepts.h"

#include "sqlitex/detail/session_api.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

class Database;

enum class ChangeRow : int {
    Old = 0,
    New = 1,
};

// ============================================================================
// Changegroup — RAII wrapper for sqlite3_changegroup*
// ============================================================================

class SQLITEX_EXPORT Changegroup {
    public:
    /// RAII builder for programmatic change construction (SQLite 3.54+).
    /// Discards the in-progress change on destruction unless commit() or
    /// discard() has been called.
    class [[nodiscard]] SQLITEX_EXPORT ChangeBuilder {
        public:
        ~ChangeBuilder();

        ChangeBuilder(ChangeBuilder&& other) noexcept;
        ChangeBuilder& operator=(ChangeBuilder&& other) noexcept;
        ChangeBuilder(const ChangeBuilder&) = delete;
        ChangeBuilder& operator=(const ChangeBuilder&) = delete;

        ChangeBuilder& set_null(ChangeRow row, int col);
        ChangeBuilder& set_int(ChangeRow row, int col, int64_t val);
        ChangeBuilder& set_double(ChangeRow row, int col, double val);
        ChangeBuilder& set_text(ChangeRow row, int col, std::string_view val);
        ChangeBuilder& set_blob(ChangeRow row, int col,
                                std::span<const std::byte> data);

        template <ConstByteRange R>
            requires(!std::same_as<std::remove_cvref_t<R>,
                                   std::span<const std::byte>>)
        ChangeBuilder& set_blob(ChangeRow row, int col, const R& data) {
            return set_blob(row, col, to_const_bytes(data));
        }

        ChangeBuilder& set(ChangeRow row, int col, std::nullptr_t) {
            return set_null(row, col);
        }
        ChangeBuilder& set(ChangeRow row, int col, bool val) {
            return set_int(row, col, val ? 1 : 0);
        }
        template <SqlInteger T>
        ChangeBuilder& set(ChangeRow row, int col, T val) {
            return set_int(row, col, static_cast<int64_t>(val));
        }
        template <SqlFloat T>
        ChangeBuilder& set(ChangeRow row, int col, T val) {
            return set_double(row, col, static_cast<double>(val));
        }
        ChangeBuilder& set(ChangeRow row, int col, const char* val) {
            if (val == nullptr) {
                return set_null(row, col);
            }
            return set_text(row, col, std::string_view(val));
        }
        ChangeBuilder& set(ChangeRow row, int col, std::string_view val) {
            return set_text(row, col, val);
        }
        ChangeBuilder& set(ChangeRow row, int col,
                           std::span<const std::byte> data) {
            return set_blob(row, col, data);
        }
        template <ConstByteRange R>
            requires(!std::same_as<std::remove_cvref_t<R>,
                                   std::span<const std::byte>>)
        ChangeBuilder& set(ChangeRow row, int col, const R& data) {
            return set_blob(row, col, to_const_bytes(data));
        }
        ChangeBuilder& set(ChangeRow row, int col, const Value& val);

        /// Commit the constructed change into the changegroup.
        void commit();

        /// Explicitly discard the in-progress change.
        void discard();

        private:
        friend class Changegroup;
        explicit ChangeBuilder(Changegroup* group) noexcept : group_(group) {
        }

        Changegroup* group_ = nullptr;
    };

    Changegroup();
    ~Changegroup();

    // Move-only
    Changegroup(Changegroup&& other) noexcept;
    Changegroup& operator=(Changegroup&& other) noexcept;
    Changegroup(const Changegroup&) = delete;
    Changegroup& operator=(const Changegroup&) = delete;

    [[nodiscard]] explicit operator bool() const noexcept {
        return group_ != nullptr;
    }
    [[nodiscard]] sqlite3_changegroup* handle() noexcept {
        return group_;
    }

    // === Configuration & Schema ===

    /// Raw wrapper for sqlite3changegroup_config().
    void config(int op, void* arg);

    /// Configure whether output is a patchset (true) or changeset (false).
    /// Returns the effective mode (true = patchset, false = changeset).
    bool set_patchset_mode(bool patchset);

    /// Query whether this changegroup produces a patchset.
    [[nodiscard]] bool is_patchset_mode();

    /// Configure schema metadata from a database (required for
    /// programmatic change construction and optional validation).
    void set_schema(Database& db, std::string_view db_name = "main");

    // === Adding Changes ===

    /// Add a changeset to the group.
    void add(const Changeset& cs);

    /// Add a patchset to the group.
    void add(const Patchset& ps);

    /// Add the single current change from a ChangesetIterator.
    void add_change(const ChangesetIterator& iter);

    // === Programmatic Change Construction (SQLite 3.54+) ===

    /// Begin an exception-safe RAII change builder for a table.
    [[nodiscard]] ChangeBuilder begin_change(ChangeOperation op,
                                             std::string_view table,
                                             bool indirect = false);

    /// Low-level programmatic change construction APIs.
    void change_begin(ChangeOperation op, std::string_view table,
                      bool indirect = false);
    void change_int64(ChangeRow row, int col, int64_t val);
    void change_double(ChangeRow row, int col, double val);
    void change_null(ChangeRow row, int col);
    void change_text(ChangeRow row, int col, std::string_view val);
    void change_blob(ChangeRow row, int col, std::span<const std::byte> data);
    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void change_blob(ChangeRow row, int col, const R& data) {
        change_blob(row, col, to_const_bytes(data));
    }
    void change_finish(bool discard = false);

    // === Output ===

    /// Output the merged changeset.
    [[nodiscard]] Changeset output();

    /// Output the merged patchset (for patchset-mode changegroups).
    [[nodiscard]] Patchset output_patchset();

    // === Streaming Operations ===

    /// Add a changeset from a streaming input callback.
    void add_stream(StreamInputFunc input);

    /// Output the merged changeset via a streaming output callback.
    void output_stream(StreamOutputFunc output);

    private:
    sqlite3_changegroup* group_ = nullptr;
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
