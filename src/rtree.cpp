#include "sqlitex/rtree.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

static_assert(std::is_same_v<sqlite3_rtree_dbl, double>,
              "sqlitex assumes floating-point R-Tree coordinates");

namespace sqlitex {

namespace {

constexpr const char* kGeomRegistryKey = "sqlitex.rtree.geom_callbacks";

struct GeomCallbackRegistry {
    std::unordered_map<std::string, std::unique_ptr<RTreeGeometryCallback>>
        callbacks;
};

int rtree_geom_shim(sqlite3_rtree_geometry* geom, int n_coord,
                    sqlite3_rtree_dbl* coords, int* p_res) noexcept {
    auto* cb = static_cast<RTreeGeometryCallback*>(geom->pContext);
    try {
        std::span<const double> param_span(
            geom->aParam,
            geom->nParam > 0 ? static_cast<std::size_t>(geom->nParam) : 0);
        std::span<const double> coord_span(
            coords, n_coord > 0 ? static_cast<std::size_t>(n_coord) : 0);
        bool const inside = (*cb)(param_span, coord_span);
        *p_res = inside ? 1 : 0;
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

void destroy_query_callback(void* ptr) noexcept {
    delete static_cast<RTreeQueryCallback*>(ptr);
}

int rtree_query_shim(sqlite3_rtree_query_info* info) noexcept {
    auto* cb = static_cast<RTreeQueryCallback*>(info->pContext);
    try {
        RTreeQueryInfo wrapper(info);
        (*cb)(wrapper);
        return SQLITE_OK;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

} // anonymous namespace

// ============================================================================
// RTreeQueryInfo
// ============================================================================

int RTreeQueryInfo::param_count() const noexcept {
    return info_->nParam;
}

double RTreeQueryInfo::param(int i) const noexcept {
    if (i < 0 || i >= info_->nParam || info_->aParam == nullptr) {
        return 0.0;
    }
    return static_cast<double>(info_->aParam[i]);
}

std::span<const double> RTreeQueryInfo::params() const noexcept {
    if (info_->nParam <= 0 || info_->aParam == nullptr) {
        return {};
    }
    return {info_->aParam, static_cast<std::size_t>(info_->nParam)};
}

ValueRef RTreeQueryInfo::sql_param(int i) const noexcept {
    if (i < 0 || i >= info_->nParam || info_->apSqlParam == nullptr) {
        return ValueRef(nullptr);
    }
    return ValueRef(info_->apSqlParam[i]);
}

int RTreeQueryInfo::coord_count() const noexcept {
    return info_->nCoord;
}

double RTreeQueryInfo::coord(int i) const noexcept {
    if (i < 0 || i >= info_->nCoord || info_->aCoord == nullptr) {
        return 0.0;
    }
    return static_cast<double>(info_->aCoord[i]);
}

std::span<const double> RTreeQueryInfo::coords() const noexcept {
    if (info_->nCoord <= 0 || info_->aCoord == nullptr) {
        return {};
    }
    return {info_->aCoord, static_cast<std::size_t>(info_->nCoord)};
}

int RTreeQueryInfo::level() const noexcept {
    return info_->iLevel;
}

int RTreeQueryInfo::max_level() const noexcept {
    return info_->mxLevel;
}

int64_t RTreeQueryInfo::rowid() const noexcept {
    return info_->iRowid;
}

double RTreeQueryInfo::parent_score() const noexcept {
    return static_cast<double>(info_->rParentScore);
}

RTreeWithin RTreeQueryInfo::parent_within() const noexcept {
    return static_cast<RTreeWithin>(info_->eParentWithin);
}

unsigned int RTreeQueryInfo::queue_count(int level) const noexcept {
    if (info_->anQueue == nullptr || level < 0 || level > info_->mxLevel) {
        return 0;
    }
    return info_->anQueue[level];
}

void RTreeQueryInfo::set_within(RTreeWithin within) noexcept {
    info_->eWithin = static_cast<int>(within);
}

void RTreeQueryInfo::set_score(double score) noexcept {
    info_->rScore = static_cast<sqlite3_rtree_dbl>(score);
}

void* RTreeQueryInfo::user_data() const noexcept {
    return info_->pUser;
}

void RTreeQueryInfo::set_user_data(void* data,
                                   void (*deleter)(void*)) noexcept {
    if (info_->pUser != nullptr && info_->xDelUser != nullptr) {
        info_->xDelUser(info_->pUser);
    }
    info_->pUser = data;
    info_->xDelUser = deleter;
}

// ============================================================================
// Registration
// ============================================================================

void register_rtree_geometry_callback(Database& db, std::string_view name,
                                      RTreeGeometryCallback callback) {
    if (!callback) {
        throw Error(ErrorCode::Misuse,
                    "R-Tree geometry callback must not be empty");
    }

    auto* registry =
        db.get_clientdata_as<GeomCallbackRegistry>(kGeomRegistryKey);
    if (registry == nullptr) {
        auto owned = std::make_unique<GeomCallbackRegistry>();
        registry = owned.get();
        db.set_clientdata(kGeomRegistryKey, std::move(owned));
    }

    std::string key(name);
    auto cb_ptr = std::make_unique<RTreeGeometryCallback>(std::move(callback));
    int const rc = sqlite3_rtree_geometry_callback(
        db.handle(), key.c_str(), rtree_geom_shim, cb_ptr.get());
    throw_on_error(rc, db.handle());

    // Store after successful registration; replacing an existing key safely
    // destroys the prior callback after SQLite has switched to the new one.
    registry->callbacks[std::move(key)] = std::move(cb_ptr);
}

void register_rtree_query_callback(Database& db, std::string_view name,
                                   RTreeQueryCallback callback) {
    if (!callback) {
        throw Error(ErrorCode::Misuse,
                    "R-Tree query callback must not be empty");
    }

    std::string key(name);
    // sqlite3_rtree_query_callback invokes xDestructor on both failure and
    // connection close, so transfer ownership immediately.
    auto* raw_cb = new RTreeQueryCallback(std::move(callback));
    int const rc =
        sqlite3_rtree_query_callback(db.handle(), key.c_str(), rtree_query_shim,
                                     raw_cb, destroy_query_callback);
    throw_on_error(rc, db.handle());
}

} // namespace sqlitex
