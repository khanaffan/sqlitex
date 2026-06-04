#include "sqlitex/changegroup.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <string>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

Changegroup::Changegroup() {
    sqlite3_changegroup* g = nullptr;
    int const rc = sqlite3changegroup_new(&g);
    throw_on_error(rc, "sqlite3changegroup_new failed");
    group_ = g;
}

Changegroup::~Changegroup() {
    if (group_ != nullptr) {
        sqlite3changegroup_delete(group_);
    }
}

Changegroup::Changegroup(Changegroup&& other) noexcept
    : group_(std::exchange(other.group_, nullptr)) {
}

Changegroup& Changegroup::operator=(Changegroup&& other) noexcept {
    if (this != &other) {
        if (group_ != nullptr) {
            sqlite3changegroup_delete(group_);
        }
        group_ = std::exchange(other.group_, nullptr);
    }
    return *this;
}

// ============================================================================
// Operations
// ============================================================================

void Changegroup::add(const Changeset& cs) {
    int const rc =
        sqlite3changegroup_add(group_, cs.size(), const_cast<void*>(cs.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add(const Patchset& ps) {
    int const rc =
        sqlite3changegroup_add(group_, ps.size(), const_cast<void*>(ps.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

Changeset Changegroup::output() {
    void* data = nullptr;
    int n = 0;

    int const rc = sqlite3changegroup_output(group_, &n, &data);
    throw_on_error(rc, "sqlite3changegroup_output failed");

    return {data, n};
}

// ============================================================================
// Streaming Operations
// ============================================================================

void Changegroup::add_stream(StreamInputFunc input) {
    void* ctx = static_cast<void*>(&input);
    int const rc = sqlite3changegroup_add_strm(
        group_, detail::stream_input_shim, ctx);
    throw_on_error(rc, "sqlite3changegroup_add_strm failed");
}

void Changegroup::output_stream(StreamOutputFunc output) {
    void* ctx = static_cast<void*>(&output);
    int const rc = sqlite3changegroup_output_strm(
        group_, detail::stream_output_shim, ctx);
    throw_on_error(rc, "sqlite3changegroup_output_strm failed");
}

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
