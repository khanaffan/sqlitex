#include "cloudsqlite/changegroup.h"
#include "cloudsqlite/error.h"

#include <sqlite3.h>

#include <string>

namespace cloudsqlite {

// ============================================================================
// Construction / Destruction
// ============================================================================

Changegroup::Changegroup() {
    sqlite3_changegroup* g = nullptr;
    int rc = sqlite3changegroup_new(&g);
    throw_on_error(rc, "sqlite3changegroup_new failed");
    group_ = g;
}

Changegroup::~Changegroup() {
    if (group_) {
        sqlite3changegroup_delete(group_);
    }
}

Changegroup::Changegroup(Changegroup&& other) noexcept
    : group_(std::exchange(other.group_, nullptr)) {}

Changegroup& Changegroup::operator=(Changegroup&& other) noexcept {
    if (this != &other) {
        if (group_) sqlite3changegroup_delete(group_);
        group_ = std::exchange(other.group_, nullptr);
    }
    return *this;
}

// ============================================================================
// Operations
// ============================================================================

void Changegroup::add(const Changeset& cs) {
    int rc = sqlite3changegroup_add(group_, cs.size(),
                                     const_cast<void*>(cs.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add(const Patchset& ps) {
    int rc = sqlite3changegroup_add(group_, ps.size(),
                                     const_cast<void*>(ps.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

Changeset Changegroup::output() {
    void* data = nullptr;
    int n = 0;

    int rc = sqlite3changegroup_output(group_, &n, &data);
    throw_on_error(rc, "sqlite3changegroup_output failed");

    return Changeset(data, n);
}

} // namespace cloudsqlite
