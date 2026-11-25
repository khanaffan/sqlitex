#pragma once

#include "sqlitex_export.h"

#include "sqlitex/changeset.h"
#include "sqlitex/detail/session_api.h"

#include <cstdint>
#include <vector>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

// ============================================================================
// Rebaser — RAII wrapper for sqlite3_rebaser*
// ============================================================================

class SQLITEX_EXPORT Rebaser {
    public:
    Rebaser();
    ~Rebaser();

    // Move-only
    Rebaser(Rebaser&& other) noexcept;
    Rebaser& operator=(Rebaser&& other) noexcept;
    Rebaser(const Rebaser&) = delete;
    Rebaser& operator=(const Rebaser&) = delete;

    explicit operator bool() const noexcept {
        return rebaser_ != nullptr;
    }
    sqlite3_rebaser* handle() noexcept {
        return rebaser_;
    }

    /// Configure the rebaser with rebase metadata produced by
    /// Changeset::apply_v2 / apply_v3 (or their streaming counterparts).
    void configure(const void* data, int size);

    /// Convenience overload taking a byte vector from apply_v2/v3 rebase_out.
    void configure(const std::vector<uint8_t>& rebase_data);

    /// Rebase a changeset according to previously configured rebase metadata.
    [[nodiscard]] Changeset rebase(const Changeset& cs);

    /// Stream-rebase a changeset from an input callback to an output callback.
    void rebase_stream(StreamInputFunc input, StreamOutputFunc output);

    private:
    sqlite3_rebaser* rebaser_ = nullptr;
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
