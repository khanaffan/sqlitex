#include "sqlitex/rebaser.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <utility>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

Rebaser::Rebaser() {
    sqlite3_rebaser* r = nullptr;
    int const rc = sqlite3rebaser_create(&r);
    throw_on_error(rc, "sqlite3rebaser_create failed");
    rebaser_ = r;
}

Rebaser::~Rebaser() {
    if (rebaser_ != nullptr) {
        sqlite3rebaser_delete(rebaser_);
    }
}

Rebaser::Rebaser(Rebaser&& other) noexcept
    : rebaser_(std::exchange(other.rebaser_, nullptr)) {
}

Rebaser& Rebaser::operator=(Rebaser&& other) noexcept {
    if (this != &other) {
        if (rebaser_ != nullptr) {
            sqlite3rebaser_delete(rebaser_);
        }
        rebaser_ = std::exchange(other.rebaser_, nullptr);
    }
    return *this;
}

void Rebaser::configure(std::span<const std::byte> rebase_data) {
    int const rc = sqlite3rebaser_configure(
        rebaser_, static_cast<int>(rebase_data.size()),
        rebase_data.empty() ? nullptr : rebase_data.data());
    throw_on_error(rc, "sqlite3rebaser_configure failed");
}

Changeset Rebaser::rebase(const Changeset& cs) {
    void* out = nullptr;
    int n_out = 0;
    int const rc = sqlite3rebaser_rebase(rebaser_, static_cast<int>(cs.size()),
                                         cs.data(), &n_out, &out);
    throw_on_error(rc, "sqlite3rebaser_rebase failed");
    return {out, n_out};
}

void Rebaser::rebase_stream(StreamInputFunc input, StreamOutputFunc output) {
    void* ctx_in = static_cast<void*>(&input);
    void* ctx_out = static_cast<void*>(&output);
    int const rc =
        sqlite3rebaser_rebase_strm(rebaser_, detail::stream_input_shim, ctx_in,
                                   detail::stream_output_shim, ctx_out);
    throw_on_error(rc, "sqlite3rebaser_rebase_strm failed");
}

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
