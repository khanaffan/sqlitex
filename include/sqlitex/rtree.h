#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace sqlitex {

class Database;

// ============================================================================
// R-Tree visibility enum (NOT_WITHIN / PARTLY_WITHIN / FULLY_WITHIN)
// ============================================================================

enum class RTreeWithin : int {
    NotWithin = 0,
    PartlyWithin = 1,
    FullyWithin = 2,
};

// ============================================================================
// RTreeQueryInfo — non-owning wrapper for sqlite3_rtree_query_info*
// ============================================================================

class SQLITEX_EXPORT RTreeQueryInfo {
    public:
    explicit RTreeQueryInfo(sqlite3_rtree_query_info* info) noexcept
        : info_(info) {
    }

    [[nodiscard]] int param_count() const noexcept;
    [[nodiscard]] double param(int i) const noexcept;
    [[nodiscard]] std::span<const double> params() const noexcept;
    [[nodiscard]] ValueRef sql_param(int i) const noexcept;

    [[nodiscard]] int coord_count() const noexcept;
    [[nodiscard]] double coord(int i) const noexcept;
    [[nodiscard]] std::span<const double> coords() const noexcept;

    [[nodiscard]] int level() const noexcept;
    [[nodiscard]] int max_level() const noexcept;
    [[nodiscard]] int64_t rowid() const noexcept;
    [[nodiscard]] double parent_score() const noexcept;
    [[nodiscard]] RTreeWithin parent_within() const noexcept;
    [[nodiscard]] unsigned int queue_count(int level) const noexcept;

    void set_within(RTreeWithin within) noexcept;
    void set_score(double score) noexcept;

    [[nodiscard]] void* user_data() const noexcept;
    void set_user_data(void* data, void (*deleter)(void*) = nullptr) noexcept;

    template <typename T>
    void set_user_data(std::unique_ptr<T> data) noexcept {
        set_user_data(data.release(), &detail::delete_typed<T>);
    }

    template <typename T>
    [[nodiscard]] T* user_data_as() const noexcept {
        return static_cast<T*>(user_data());
    }

    [[nodiscard]] sqlite3_rtree_query_info* handle() noexcept {
        return info_;
    }

    private:
    sqlite3_rtree_query_info* info_;
};

// ============================================================================
// Callback types & registration functions
// ============================================================================

/// 1st-generation R-Tree geometry callback (sqlite3_rtree_geometry_callback).
/// Return true if the bounding box defined by `coords` overlaps
/// the query geometry defined by `params`.
using RTreeGeometryCallback = std::function<bool(
    std::span<const double> params, std::span<const double> coords)>;

/// 2nd-generation R-Tree scored query callback (sqlite3_rtree_query_callback).
/// Populate info.set_within(...) and optionally info.set_score(...).
using RTreeQueryCallback = std::function<void(RTreeQueryInfo&)>;

/// Register a 1st-generation R-Tree geometry callback on a database.
/// The callback lifetime is bound to the Database connection.
SQLITEX_EXPORT void register_rtree_geometry_callback(
    Database& db, std::string_view name, RTreeGeometryCallback callback);

/// Register a 2nd-generation R-Tree query callback on a database.
/// The callback lifetime is bound to the Database connection.
SQLITEX_EXPORT void register_rtree_query_callback(Database& db,
                                                  std::string_view name,
                                                  RTreeQueryCallback callback);

} // namespace sqlitex
