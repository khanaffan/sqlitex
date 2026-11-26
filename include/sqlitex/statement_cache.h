#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/error.h"
#include "sqlitex/statement.h"
#include "sqlitex/value.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace sqlitex {

class Database;
class StatementCache;

namespace detail {
struct StatementCacheState;
} // namespace detail

// ============================================================================
// StatementCacheStats — cache telemetry snapshot
// ============================================================================

struct StatementCacheStats {
    std::size_t hits = 0;      ///< Checkouts satisfied from the cache.
    std::size_t misses = 0;    ///< Checkouts that required sqlite3_prepare.
    std::size_t evictions = 0; ///< Idle statements evicted due to capacity.
    std::size_t bypasses = 0;  ///< Checkouts bypassing cache (policy / cap 0).
};

// ============================================================================
// CachedStatement — RAII lease for a cached Statement
// ============================================================================

/// Move-only RAII handle returned by StatementCache::get() and
/// Database::prepare_cached().
///
/// On destruction (or move-assignment), the wrapped Statement is automatically
/// reset and its bindings are cleared before returning it to the originating
/// cache. If the lease was discarded, the cache was cleared/destroyed,
/// capacity is 0, or the statement cannot be cleanly reset, it is finalized
/// instead.
class SQLITEX_EXPORT CachedStatement {
    public:
    CachedStatement() noexcept = default;
    ~CachedStatement();

    // Move-only
    CachedStatement(CachedStatement&& other) noexcept;
    CachedStatement& operator=(CachedStatement&& other) noexcept;
    CachedStatement(const CachedStatement&) = delete;
    CachedStatement& operator=(const CachedStatement&) = delete;

    explicit operator bool() const noexcept {
        return static_cast<bool>(stmt_);
    }

    Statement* operator->() noexcept {
        return &stmt_;
    }
    const Statement* operator->() const noexcept {
        return &stmt_;
    }

    Statement& operator*() & noexcept {
        return stmt_;
    }
    const Statement& operator*() const& noexcept {
        return stmt_;
    }

    Statement& statement() & noexcept {
        return stmt_;
    }
    const Statement& statement() const& noexcept {
        return stmt_;
    }

    /// True if this checkout reused an existing prepared statement from cache.
    [[nodiscard]] bool from_cache() const noexcept {
        return from_cache_;
    }

    /// True if this statement will be returned to the cache on destruction.
    [[nodiscard]] bool is_cacheable() const noexcept {
        return cacheable_;
    }

    /// Opt out of returning this statement to the cache on destruction.
    void discard() noexcept {
        cacheable_ = false;
    }

    /// Detach and take ownership of the underlying Statement without returning
    /// it to the cache.
    [[nodiscard]] Statement release() noexcept;

    private:
    friend class StatementCache;

    CachedStatement(Statement stmt,
                    std::weak_ptr<detail::StatementCacheState> cache_state,
                    std::shared_ptr<const std::string> key_sql,
                    PrepareFlags key_flags, uint64_t generation, bool cacheable,
                    bool from_cache) noexcept;

    void return_or_finalize() noexcept;

    Statement stmt_{};
    std::weak_ptr<detail::StatementCacheState> cache_state_{};
    std::shared_ptr<const std::string> key_sql_{};
    PrepareFlags key_flags_{PrepareFlag::None};
    uint64_t generation_ = 0;
    bool cacheable_ = false;
    bool from_cache_ = false;
};

// ============================================================================
// StatementCache — thread-safe bounded LRU cache of prepared statements
// ============================================================================

/// Thread-safe (mutex-synchronized), bounded LRU cache of prepared SQLite
/// statements associated with a Database connection.
///
/// Design highlights:
/// - Statements are prepared with PrepareFlag::Persistent (avoiding SQLite
///   lookaside memory) and keyed by (sql, normalized PrepareFlags).
/// - Concurrent or nested/re-entrant checkouts of the same SQL receive
///   independent sqlite3_stmt handles and return them cleanly to the LRU pool.
/// - While a ScopedPolicy is active on the connection, checkouts automatically
///   bypass the cache (both lookup and insertion) so authorizer checks always
///   execute and redacted bytecode never leaks past the policy scope.
/// - All SQLite C calls (prepare, reset, clear_bindings, finalize) execute
///   outside the cache mutex to avoid deadlocks with SQLite's internal mutex
///   and user hooks.
///
/// Precondition: the referenced Database must remain valid for the lifetime of
/// any standalone StatementCache (Database's built-in cache is cleared
/// automatically before close/destruction).
class SQLITEX_EXPORT StatementCache {
    public:
    static constexpr std::size_t kDefaultCapacity = 64;

    StatementCache() noexcept = default;
    explicit StatementCache(Database& db,
                            std::size_t capacity = kDefaultCapacity);
    ~StatementCache();

    // Move-only
    StatementCache(StatementCache&& other) noexcept;
    StatementCache& operator=(StatementCache&& other) noexcept;
    StatementCache(const StatementCache&) = delete;
    StatementCache& operator=(const StatementCache&) = delete;

    explicit operator bool() const noexcept {
        return state_ != nullptr;
    }

    // === Checkout API ===

    /// Obtain a cached (or freshly prepared) single SQL statement.
    /// Throws MisuseError if the cache is unbound, the database is closed, or
    /// `sql` contains more than one SQL statement.
    [[nodiscard]] CachedStatement get(std::string_view sql,
                                      PrepareFlags flags = PrepareFlag::None);

    /// Non-throwing checkout returning std::expected<CachedStatement, Error>.
    [[nodiscard]] Expected<CachedStatement> try_get(
        std::string_view sql, PrepareFlags flags = PrepareFlag::None);

    // === Convenience Parameterized Execution ===

    /// Check out a cached statement, bind optional arguments, and step once.
    template <Bindable... Args>
    void execute(std::string_view sql, Args&&... args);

    /// Check out a cached statement, bind optional arguments, and collect all
    /// rows as Value vectors.
    template <Bindable... Args>
    [[nodiscard]] std::vector<std::vector<Value>> query(std::string_view sql,
                                                        Args&&... args);

    /// Check out a cached statement, bind optional arguments, and return the
    /// first column of the first row (std::nullopt on no rows or SQL NULL for
    /// non-Value, non-optional types).
    template <OwnedColumnValue T = int64_t, Bindable... Args>
    [[nodiscard]] std::optional<T> query_value(std::string_view sql,
                                               Args&&... args);

    // === Capacity, Introspection & Maintenance ===

    /// Number of idle statements currently stored in the cache.
    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    /// Maximum number of idle statements retained by the cache (0 = disabled).
    [[nodiscard]] std::size_t capacity() const noexcept;

    /// Update the cache capacity, immediately evicting LRU entries if needed.
    /// Setting capacity to 0 disables caching and clears all idle entries.
    void set_capacity(std::size_t capacity);

    /// Check whether at least one idle statement for `(sql, flags)` is cached.
    [[nodiscard]] bool contains(
        std::string_view sql,
        PrepareFlags flags = PrepareFlag::None) const noexcept;

    /// Remove all idle cached statements matching `(sql, flags)`.
    /// Returns the number of statements removed.
    std::size_t erase(std::string_view sql,
                      PrepareFlags flags = PrepareFlag::None);

    /// Finalize all idle cached statements and invalidate in-flight leases so
    /// statements checked out before clear() will not re-enter the cache.
    void clear() noexcept;

    /// Snapshot current hit/miss/eviction/bypass counters.
    [[nodiscard]] StatementCacheStats stats() const noexcept;

    /// Reset all cache statistics counters to zero.
    void reset_stats() noexcept;

    private:
    friend class Database;

    void rebind_database(Database& db) noexcept;

    std::shared_ptr<detail::StatementCacheState> state_{};
};

// ============================================================================
// Template implementations
// ============================================================================

template <Bindable... Args>
void StatementCache::execute(std::string_view sql, Args&&... args) {
    CachedStatement stmt = get(sql);
    if (!stmt) {
        return;
    }
    if constexpr (sizeof...(Args) > 0) {
        stmt->bind_all(std::forward<Args>(args)...);
    }
    (void)stmt->step();
}

template <Bindable... Args>
std::vector<std::vector<Value>> StatementCache::query(std::string_view sql,
                                                      Args&&... args) {
    CachedStatement stmt = get(sql);
    std::vector<std::vector<Value>> rows;
    if (!stmt) {
        return rows;
    }
    if constexpr (sizeof...(Args) > 0) {
        stmt->bind_all(std::forward<Args>(args)...);
    }
    while (stmt->step()) {
        int const ncols = stmt->column_count();
        std::vector<Value> row;
        row.reserve(static_cast<std::size_t>(ncols));
        for (int i = 0; i < ncols; ++i) {
            row.push_back(stmt->column_value(i));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

template <OwnedColumnValue T, Bindable... Args>
std::optional<T> StatementCache::query_value(std::string_view sql,
                                             Args&&... args) {
    CachedStatement stmt = get(sql);
    if (!stmt) {
        return std::nullopt;
    }
    if constexpr (sizeof...(Args) > 0) {
        stmt->bind_all(std::forward<Args>(args)...);
    }
    if (!stmt->step() || stmt->column_count() == 0) {
        return std::nullopt;
    }
    using Decayed = std::remove_cvref_t<T>;
    if constexpr (!std::same_as<Decayed, Value> &&
                  !detail::OptionalInner<Decayed>::is_optional) {
        if (stmt->column_is_null(0)) {
            return std::nullopt;
        }
    }
    return stmt->column_as<T>(0);
}

} // namespace sqlitex
