#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/value.h"

#include <cstdint>
#include <functional>
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

    int param_count() const noexcept;
    double param(int i) const noexcept;
    ValueRef sql_param(int i) const noexcept;

    int coord_count() const noexcept;
    double coord(int i) const noexcept;

    int level() const noexcept;
    int max_level() const noexcept;
    int64_t rowid() const noexcept;
    double parent_score() const noexcept;
    RTreeWithin parent_within() const noexcept;
    unsigned int queue_count(int level) const noexcept;

    void set_within(RTreeWithin within) noexcept;
    void set_score(double score) noexcept;

    void* user_data() const noexcept;
    void set_user_data(void* data, void (*deleter)(void*) = nullptr) noexcept;

    sqlite3_rtree_query_info* handle() noexcept {
        return info_;
    }

    private:
    sqlite3_rtree_query_info* info_;
};

// ============================================================================
// Callback types & registration functions
// ============================================================================

/// 1st-generation R-Tree geometry callback (sqlite3_rtree_geometry_callback).
/// Return true if the bounding box defined by coords[0..n_coords-1] overlaps
/// the query geometry defined by params[0..n_params-1].
using RTreeGeometryCallback = std::function<bool(
    const double* params, int n_params, const double* coords, int n_coords)>;

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
