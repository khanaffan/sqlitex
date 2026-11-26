#include "sqlitex/statement_cache.h"

#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <cassert>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace sqlitex {

namespace {

constexpr PrepareFlags normalize_cache_flags(PrepareFlags flags) noexcept {
    return flags | PrepareFlag::Persistent;
}

/// Returns true if `tail` contains any non-trivia SQL tokens (i.e., anything
/// other than whitespace, semicolons, `-- ...` line comments, or `/* ... */`
/// block comments).
bool has_trailing_sql(std::string_view tail) noexcept {
    std::size_t i = 0;
    std::size_t const n = tail.size();
    while (i < n) {
        char const c = tail[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '\v' || c == ';') {
            ++i;
            continue;
        }
        if (c == '-' && i + 1 < n && tail[i + 1] == '-') {
            i += 2;
            while (i < n && tail[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < n && tail[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(tail[i] == '*' && tail[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < n) {
                i += 2;
            } else {
                // SQLite treats an unterminated block comment at EOF as a
                // comment; no trailing SQL statement follows.
                return false;
            }
            continue;
        }
        return true;
    }
    return false;
}

struct OwnedKey {
    std::shared_ptr<const std::string> sql;
    PrepareFlags flags{PrepareFlag::None};
};

struct KeyView {
    std::string_view sql;
    PrepareFlags flags{PrepareFlag::None};
};

struct KeyHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view sql,
                           PrepareFlags flags) const noexcept {
        std::size_t const h1 = std::hash<std::string_view>{}(sql);
        std::size_t const h2 = std::hash<unsigned int>{}(flags.raw());
        if constexpr (sizeof(std::size_t) >= 8) {
            return h1 ^ (h2 + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
                         (h1 << 6U) + (h1 >> 2U));
        } else {
            return h1 ^ (h2 + static_cast<std::size_t>(0x9e3779b9U) +
                         (h1 << 6U) + (h1 >> 2U));
        }
    }

    std::size_t operator()(const KeyView& k) const noexcept {
        return (*this)(k.sql, k.flags);
    }

    std::size_t operator()(const OwnedKey& k) const noexcept {
        return (*this)(*k.sql, k.flags);
    }
};

struct KeyEqual {
    using is_transparent = void;

    bool operator()(const OwnedKey& a, const OwnedKey& b) const noexcept {
        return a.flags == b.flags && *a.sql == *b.sql;
    }
    bool operator()(const OwnedKey& a, const KeyView& b) const noexcept {
        return a.flags == b.flags && *a.sql == b.sql;
    }
    bool operator()(const KeyView& a, const OwnedKey& b) const noexcept {
        return a.flags == b.flags && a.sql == *b.sql;
    }
    bool operator()(const KeyView& a, const KeyView& b) const noexcept {
        return a.flags == b.flags && a.sql == b.sql;
    }
};

struct CacheEntry {
    std::shared_ptr<const std::string> sql;
    PrepareFlags flags{PrepareFlag::None};
    Statement stmt;
};

} // namespace

namespace detail {

struct StatementCacheState {
    using LruList = std::list<CacheEntry>;
    using LruIter = LruList::iterator;

    mutable std::mutex mutex;
    Database* db = nullptr;
    std::size_t capacity = StatementCache::kDefaultCapacity;
    uint64_t generation = 0;
    bool detached = false;
    LruList lru;
    std::unordered_map<OwnedKey, std::deque<LruIter>, KeyHash, KeyEqual> index;
    StatementCacheStats stats{};

    StatementCacheState(Database* database, std::size_t cap) noexcept
        : db(database), capacity(cap) {
    }

    /// Evict oldest entries until lru.size() <= capacity.
    /// Caller must hold `mutex`. Evicted entries are spliced (noexcept,
    /// zero-allocation) into `evicted` so their sqlite3_stmt handles finalize
    /// AFTER releasing `mutex`.
    void evict_to_capacity_locked(LruList& evicted) noexcept {
        while (lru.size() > capacity) {
            auto last_it = std::prev(lru.end());
            auto map_it = index.find(KeyView{*last_it->sql, last_it->flags});
            assert(map_it != index.end() && !map_it->second.empty() &&
                   map_it->second.front() == last_it);
            if (map_it != index.end() && !map_it->second.empty()) {
                map_it->second.pop_front();
                if (map_it->second.empty()) {
                    index.erase(map_it);
                }
            }
            evicted.splice(evicted.end(), lru, last_it);
            ++stats.evictions;
        }
    }
};

} // namespace detail

// ============================================================================
// CachedStatement
// ============================================================================

CachedStatement::CachedStatement(
    Statement stmt, std::weak_ptr<detail::StatementCacheState> cache_state,
    std::shared_ptr<const std::string> key_sql, PrepareFlags key_flags,
    uint64_t generation, bool cacheable, bool from_cache) noexcept
    : stmt_(std::move(stmt)),
      cache_state_(std::move(cache_state)),
      key_sql_(std::move(key_sql)),
      key_flags_(key_flags),
      generation_(generation),
      cacheable_(cacheable),
      from_cache_(from_cache) {
}

CachedStatement::~CachedStatement() {
    return_or_finalize();
}

CachedStatement::CachedStatement(CachedStatement&& other) noexcept
    : stmt_(std::move(other.stmt_)),
      cache_state_(std::move(other.cache_state_)),
      key_sql_(std::move(other.key_sql_)),
      key_flags_(std::exchange(other.key_flags_, PrepareFlag::None)),
      generation_(std::exchange(other.generation_, 0)),
      cacheable_(std::exchange(other.cacheable_, false)),
      from_cache_(std::exchange(other.from_cache_, false)) {
}

CachedStatement& CachedStatement::operator=(CachedStatement&& other) noexcept {
    if (this != &other) {
        return_or_finalize();
        stmt_ = std::move(other.stmt_);
        cache_state_ = std::move(other.cache_state_);
        key_sql_ = std::move(other.key_sql_);
        key_flags_ = std::exchange(other.key_flags_, PrepareFlag::None);
        generation_ = std::exchange(other.generation_, 0);
        cacheable_ = std::exchange(other.cacheable_, false);
        from_cache_ = std::exchange(other.from_cache_, false);
    }
    return *this;
}

Statement CachedStatement::release() noexcept {
    cache_state_.reset();
    key_sql_.reset();
    key_flags_ = PrepareFlag::None;
    generation_ = 0;
    cacheable_ = false;
    from_cache_ = false;
    return std::move(stmt_);
}

void CachedStatement::return_or_finalize() noexcept {
    if (!stmt_) {
        cache_state_.reset();
        key_sql_.reset();
        key_flags_ = PrepareFlag::None;
        generation_ = 0;
        cacheable_ = false;
        from_cache_ = false;
        return;
    }

    if (cacheable_ && key_sql_) {
        if (auto state = cache_state_.lock()) {
            sqlite3_stmt* raw = stmt_.handle();

            // Reset the statement and clear bindings OUTSIDE the cache mutex.
            // sqlite3_reset echoes the last sqlite3_step error code (e.g.
            // SQLITE_CONSTRAINT) while still resetting the VM; calling it a
            // second time verifies the VM is cleanly reset (SQLITE_OK).
            int const reset_rc = sqlite3_reset(raw);
            bool clean = true;
            if (reset_rc != SQLITE_OK) {
                if ((reset_rc & 0xFF) == SQLITE_NOMEM ||
                    sqlite3_reset(raw) != SQLITE_OK) {
                    clean = false;
                }
            }
            if (clean && (sqlite3_stmt_busy(raw) != 0 ||
                          sqlite3_clear_bindings(raw) != SQLITE_OK)) {
                clean = false;
            }

            if (clean) {
                detail::StatementCacheState::LruList evicted;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (!state->detached && state->capacity > 0 &&
                        state->generation == generation_ &&
                        state->db != nullptr &&
                        state->db->handle() == sqlite3_db_handle(raw)) {
                        try {
                            state->lru.push_front(
                                CacheEntry{key_sql_, key_flags_, Statement{}});
                            auto list_it = state->lru.begin();
                            try {
                                auto map_it = state->index.find(
                                    KeyView{*list_it->sql, list_it->flags});
                                if (map_it != state->index.end()) {
                                    map_it->second.push_back(list_it);
                                } else {
                                    state->index.emplace(
                                        OwnedKey{list_it->sql, list_it->flags},
                                        std::deque<detail::StatementCacheState::
                                                       LruIter>{list_it});
                                }
                            } catch (...) {
                                state->lru.pop_front();
                                throw;
                            }
                            list_it->stmt = std::move(stmt_);
                            state->evict_to_capacity_locked(evicted);
                        } catch (...) {
                            // Destructor must not throw; `stmt_` remains intact
                            // and will be finalized outside the lock below.
                        }
                    }
                }
                // `evicted` finalizes here, outside `state->mutex`.
            }
        }
    }

    // If not moved back into the cache, finalize outside any lock.
    stmt_ = Statement{};
    cache_state_.reset();
    key_sql_.reset();
    key_flags_ = PrepareFlag::None;
    generation_ = 0;
    cacheable_ = false;
    from_cache_ = false;
}

// ============================================================================
// StatementCache
// ============================================================================

StatementCache::StatementCache(Database& db, std::size_t capacity)
    : state_(std::make_shared<detail::StatementCacheState>(&db, capacity)) {
}

StatementCache::~StatementCache() {
    if (state_) {
        detail::StatementCacheState::LruList evicted;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->detached = true;
            state_->db = nullptr;
            ++state_->generation;
            evicted.swap(state_->lru);
            state_->index.clear();
        }
        // `evicted` finalizes all cached statements outside `state_->mutex`.
    }
}

StatementCache::StatementCache(StatementCache&& other) noexcept
    : state_(std::move(other.state_)) {
}

StatementCache& StatementCache::operator=(StatementCache&& other) noexcept {
    if (this != &other) {
        if (state_) {
            detail::StatementCacheState::LruList evicted;
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                state_->detached = true;
                state_->db = nullptr;
                ++state_->generation;
                evicted.swap(state_->lru);
                state_->index.clear();
            }
        }
        state_ = std::move(other.state_);
    }
    return *this;
}

CachedStatement StatementCache::get(std::string_view sql, PrepareFlags flags) {
    auto state = state_;
    if (!state) {
        throw MisuseError(ErrorCode::Misuse,
                          "StatementCache is not bound to a Database");
    }

    PrepareFlags const norm_flags = normalize_cache_flags(flags);
    Database* db = nullptr;
    uint64_t gen = 0;
    bool bypass = false;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->detached || state->db == nullptr) {
            throw MisuseError(ErrorCode::Misuse,
                              "StatementCache is not bound to a Database");
        }
        db = state->db;
        if (!(*db)) {
            throw MisuseError(ErrorCode::Misuse, "Database is not open");
        }

        gen = state->generation;
        if (db->has_active_policy() || state->capacity == 0) {
            bypass = true;
            ++state->stats.bypasses;
        } else {
            auto map_it = state->index.find(KeyView{sql, norm_flags});
            if (map_it != state->index.end() && !map_it->second.empty()) {
                auto list_it = map_it->second.back();
                map_it->second.pop_back();
                auto key_sql = std::move(list_it->sql);
                Statement stmt = std::move(list_it->stmt);
                state->lru.erase(list_it);
                if (map_it->second.empty()) {
                    state->index.erase(map_it);
                }
                ++state->stats.hits;
                return CachedStatement(std::move(stmt), state,
                                       std::move(key_sql), norm_flags, gen,
                                       /*cacheable=*/true,
                                       /*from_cache=*/true);
            }
            ++state->stats.misses;
        }
    }

    // Prepare outside `state->mutex` to avoid deadlocks with SQLite's
    // connection mutex and user hooks (authorizer, trace, collation_needed).
    std::string_view tail;
    PrepareFlags const prep_flags = bypass ? flags : norm_flags;
    Statement stmt = db->prepare_v3(sql, prep_flags, &tail);
    if (has_trailing_sql(tail)) {
        throw MisuseError(ErrorCode::Misuse,
                          "StatementCache only caches single SQL statements");
    }

    bool const cacheable = !bypass && static_cast<bool>(stmt);
    std::shared_ptr<const std::string> key_sql;
    if (cacheable) {
        key_sql = std::make_shared<const std::string>(sql);
    }
    return CachedStatement(
        std::move(stmt),
        cacheable ? std::weak_ptr<detail::StatementCacheState>(state)
                  : std::weak_ptr<detail::StatementCacheState>{},
        std::move(key_sql), norm_flags, gen, cacheable, /*from_cache=*/false);
}

Expected<CachedStatement> StatementCache::try_get(std::string_view sql,
                                                  PrepareFlags flags) {
    auto state = state_;
    if (!state) {
        return std::unexpected(Error(
            ErrorCode::Misuse, "StatementCache is not bound to a Database"));
    }

    PrepareFlags const norm_flags = normalize_cache_flags(flags);
    Database* db = nullptr;
    uint64_t gen = 0;
    bool bypass = false;

    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->detached || state->db == nullptr) {
            return std::unexpected(
                Error(ErrorCode::Misuse,
                      "StatementCache is not bound to a Database"));
        }
        db = state->db;
        if (!(*db)) {
            return std::unexpected(
                Error(ErrorCode::Misuse, "Database is not open"));
        }

        gen = state->generation;
        if (db->has_active_policy() || state->capacity == 0) {
            bypass = true;
            ++state->stats.bypasses;
        } else {
            auto map_it = state->index.find(KeyView{sql, norm_flags});
            if (map_it != state->index.end() && !map_it->second.empty()) {
                auto list_it = map_it->second.back();
                map_it->second.pop_back();
                auto key_sql = std::move(list_it->sql);
                Statement stmt = std::move(list_it->stmt);
                state->lru.erase(list_it);
                if (map_it->second.empty()) {
                    state->index.erase(map_it);
                }
                ++state->stats.hits;
                return CachedStatement(std::move(stmt), state,
                                       std::move(key_sql), norm_flags, gen,
                                       /*cacheable=*/true,
                                       /*from_cache=*/true);
            }
            ++state->stats.misses;
        }
    }

    std::string_view tail;
    PrepareFlags const prep_flags = bypass ? flags : norm_flags;
    auto prepared = db->try_prepare(sql, prep_flags, &tail);
    if (!prepared) {
        return std::unexpected(std::move(prepared.error()));
    }
    if (has_trailing_sql(tail)) {
        return std::unexpected(
            Error(ErrorCode::Misuse,
                  "StatementCache only caches single SQL statements"));
    }

    Statement stmt = std::move(*prepared);
    bool const cacheable = !bypass && static_cast<bool>(stmt);
    std::shared_ptr<const std::string> key_sql;
    if (cacheable) {
        key_sql = std::make_shared<const std::string>(sql);
    }
    return CachedStatement(
        std::move(stmt),
        cacheable ? std::weak_ptr<detail::StatementCacheState>(state)
                  : std::weak_ptr<detail::StatementCacheState>{},
        std::move(key_sql), norm_flags, gen, cacheable, /*from_cache=*/false);
}

std::size_t StatementCache::size() const noexcept {
    auto state = state_;
    if (!state) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->lru.size();
}

std::size_t StatementCache::capacity() const noexcept {
    auto state = state_;
    if (!state) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->capacity;
}

void StatementCache::set_capacity(std::size_t capacity) {
    auto state = state_;
    if (!state) {
        throw MisuseError(ErrorCode::Misuse,
                          "StatementCache is not bound to a Database");
    }
    detail::StatementCacheState::LruList evicted;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->capacity = capacity;
        state->evict_to_capacity_locked(evicted);
    }
    // `evicted` finalizes outside `state->mutex`.
}

bool StatementCache::contains(std::string_view sql,
                              PrepareFlags flags) const noexcept {
    auto state = state_;
    if (!state) {
        return false;
    }
    PrepareFlags const norm_flags = normalize_cache_flags(flags);
    std::lock_guard<std::mutex> lock(state->mutex);
    auto it = state->index.find(KeyView{sql, norm_flags});
    return it != state->index.end() && !it->second.empty();
}

std::size_t StatementCache::erase(std::string_view sql, PrepareFlags flags) {
    auto state = state_;
    if (!state) {
        return 0;
    }
    PrepareFlags const norm_flags = normalize_cache_flags(flags);
    std::size_t removed = 0;
    detail::StatementCacheState::LruList evicted;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto it = state->index.find(KeyView{sql, norm_flags});
        if (it != state->index.end()) {
            removed = it->second.size();
            for (auto list_it : it->second) {
                evicted.splice(evicted.end(), state->lru, list_it);
            }
            state->index.erase(it);
        }
    }
    // `evicted` finalizes outside `state->mutex`.
    return removed;
}

void StatementCache::clear() noexcept {
    auto state = state_;
    if (!state) {
        return;
    }
    detail::StatementCacheState::LruList evicted;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->generation;
        evicted.swap(state->lru);
        state->index.clear();
    }
    // `evicted` finalizes outside `state->mutex`.
}

StatementCacheStats StatementCache::stats() const noexcept {
    auto state = state_;
    if (!state) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    return state->stats;
}

void StatementCache::reset_stats() noexcept {
    auto state = state_;
    if (!state) {
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stats = {};
}

void StatementCache::rebind_database(Database& db) noexcept {
    auto state = state_;
    if (!state) {
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    state->db = &db;
    for (auto& entry : state->lru) {
        entry.stmt.db_ = &db;
    }
}

} // namespace sqlitex
