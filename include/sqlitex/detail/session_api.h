#pragma once

#ifdef SQLITEX_HAS_SESSION

/// Forward declarations and prototypes for the SQLite Session Extension.
///
/// Some distributions of SQLite (including Homebrew) ship the session
/// extension functions in the library but do NOT expose the types or
/// constants in sqlite3.h (they are gated behind SQLITE_ENABLE_SESSION
/// which is an internal compile-time flag).
///
/// This header provides the minimum declarations needed to use the session
/// API.  If your SQLite build exposes these in sqlite3.h, define
/// SQLITEX_SESSION_IN_HEADER to skip these declarations.

#ifndef SQLITEX_SESSION_IN_HEADER

// sqlite3, sqlite3_value and sqlite3_int64 (referenced by the prototypes
// below) are forward-declared here without pulling in <sqlite3.h>.
#include "sqlitex/detail/sqlite3_fwd.h"

// Opaque types
struct sqlite3_session;
struct sqlite3_changeset_iter;
struct sqlite3_changegroup;
struct sqlite3_rebaser;

#ifdef __cplusplus
extern "C" {
#endif

// Session lifecycle
int sqlite3session_create(sqlite3*, const char*, sqlite3_session**);
void sqlite3session_delete(sqlite3_session*);
int sqlite3session_enable(sqlite3_session*, int);
int sqlite3session_indirect(sqlite3_session*, int);
int sqlite3session_attach(sqlite3_session*, const char*);
int sqlite3session_table_filter(sqlite3_session*, int (*)(void*, const char*),
                                void*);
int sqlite3session_changeset(sqlite3_session*, int*, void**);
sqlite3_int64 sqlite3session_changeset_size(sqlite3_session*);
int sqlite3session_patchset(sqlite3_session*, int*, void**);
int sqlite3session_diff(sqlite3_session*, const char*, const char*, char**);
int sqlite3session_isempty(sqlite3_session*);
sqlite3_int64 sqlite3session_memory_used(sqlite3_session*);
int sqlite3session_object_config(sqlite3_session*, int, void*);
int sqlite3session_config(int, void*);

// Session streaming extraction
int sqlite3session_changeset_strm(sqlite3_session*,
                                  int (*xOutput)(void*, const void*, int),
                                  void*);
int sqlite3session_patchset_strm(sqlite3_session*,
                                 int (*xOutput)(void*, const void*, int),
                                 void*);

// Changeset iteration
int sqlite3changeset_start(sqlite3_changeset_iter**, int, void*);
int sqlite3changeset_start_v2(sqlite3_changeset_iter**, int, void*, int);
int sqlite3changeset_finalize(sqlite3_changeset_iter*);
int sqlite3changeset_next(sqlite3_changeset_iter*);
int sqlite3changeset_op(sqlite3_changeset_iter*, const char**, int*, int*,
                        int*);
int sqlite3changeset_pk(sqlite3_changeset_iter*, unsigned char**, int*);
int sqlite3changeset_old(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_new(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_conflict(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_fk_conflicts(sqlite3_changeset_iter*, int*);

// Changeset streaming iteration
int sqlite3changeset_start_strm(sqlite3_changeset_iter**,
                                int (*xInput)(void*, void*, int*), void*);
int sqlite3changeset_start_v2_strm(sqlite3_changeset_iter**,
                                   int (*xInput)(void*, void*, int*), void*,
                                   int);

// Changeset manipulation
int sqlite3changeset_concat(int, void*, int, void*, int*, void**);
int sqlite3changeset_invert(int, void*, int*, void**);

// Changeset streaming manipulation
int sqlite3changeset_concat_strm(int (*xInputA)(void*, void*, int*), void*,
                                 int (*xInputB)(void*, void*, int*), void*,
                                 int (*xOutput)(void*, const void*, int),
                                 void*);
int sqlite3changeset_invert_strm(int (*xInput)(void*, void*, int*), void*,
                                 int (*xOutput)(void*, const void*, int),
                                 void*);

// Changeset application
int sqlite3changeset_apply(sqlite3*, int, void*, int (*)(void*, const char*),
                           int (*)(void*, int, sqlite3_changeset_iter*), void*);
int sqlite3changeset_apply_v2(sqlite3*, int, void*, int (*)(void*, const char*),
                              int (*)(void*, int, sqlite3_changeset_iter*),
                              void*, void**, int*, int);
int sqlite3changeset_apply_v3(sqlite3*, int, void*,
                              int (*)(void*, sqlite3_changeset_iter*),
                              int (*)(void*, int, sqlite3_changeset_iter*),
                              void*, void**, int*, int);

// Changeset streaming application
int sqlite3changeset_apply_strm(sqlite3*, int (*xInput)(void*, void*, int*),
                                void*, int (*xFilter)(void*, const char*),
                                int (*xConflict)(void*, int,
                                                 sqlite3_changeset_iter*),
                                void*);
int sqlite3changeset_apply_v2_strm(sqlite3*, int (*xInput)(void*, void*, int*),
                                   void*, int (*xFilter)(void*, const char*),
                                   int (*xConflict)(void*, int,
                                                    sqlite3_changeset_iter*),
                                   void*, void**, int*, int);
int sqlite3changeset_apply_v3_strm(
    sqlite3*, int (*xInput)(void*, void*, int*), void*,
    int (*xFilter)(void*, sqlite3_changeset_iter*),
    int (*xConflict)(void*, int, sqlite3_changeset_iter*), void*, void**, int*,
    int);

// Changegroup
int sqlite3changegroup_new(sqlite3_changegroup**);
void sqlite3changegroup_delete(sqlite3_changegroup*);
int sqlite3changegroup_config(sqlite3_changegroup*, int, void*);
int sqlite3changegroup_schema(sqlite3_changegroup*, sqlite3*, const char*);
int sqlite3changegroup_add(sqlite3_changegroup*, int, void*);
int sqlite3changegroup_add_change(sqlite3_changegroup*,
                                  sqlite3_changeset_iter*);
int sqlite3changegroup_output(sqlite3_changegroup*, int*, void**);
int sqlite3changegroup_change_begin(sqlite3_changegroup*, int, const char*, int,
                                    char**);
int sqlite3changegroup_change_int64(sqlite3_changegroup*, int, int,
                                    sqlite3_int64);
int sqlite3changegroup_change_null(sqlite3_changegroup*, int, int);
int sqlite3changegroup_change_double(sqlite3_changegroup*, int, int, double);
int sqlite3changegroup_change_text(sqlite3_changegroup*, int, int, const char*,
                                   int);
int sqlite3changegroup_change_blob(sqlite3_changegroup*, int, int, const void*,
                                   int);
int sqlite3changegroup_change_finish(sqlite3_changegroup*, int, char**);

// Changegroup streaming
int sqlite3changegroup_add_strm(sqlite3_changegroup*,
                                int (*xInput)(void*, void*, int*), void*);
int sqlite3changegroup_output_strm(sqlite3_changegroup*,
                                   int (*xOutput)(void*, const void*, int),
                                   void*);

// Rebaser
int sqlite3rebaser_create(sqlite3_rebaser**);
int sqlite3rebaser_configure(sqlite3_rebaser*, int, const void*);
int sqlite3rebaser_rebase(sqlite3_rebaser*, int, const void*, int*, void**);
int sqlite3rebaser_rebase_strm(sqlite3_rebaser*,
                               int (*xInput)(void*, void*, int*), void*,
                               int (*xOutput)(void*, const void*, int), void*);
void sqlite3rebaser_delete(sqlite3_rebaser*);

// Constants (only define if not already provided by sqlite3.h)
#ifndef SQLITE_CHANGESET_DATA
#define SQLITE_CHANGESET_DATA 1
#endif
#ifndef SQLITE_CHANGESET_NOTFOUND
#define SQLITE_CHANGESET_NOTFOUND 2
#endif
#ifndef SQLITE_CHANGESET_CONFLICT
#define SQLITE_CHANGESET_CONFLICT 3
#endif
#ifndef SQLITE_CHANGESET_CONSTRAINT
#define SQLITE_CHANGESET_CONSTRAINT 4
#endif
#ifndef SQLITE_CHANGESET_FOREIGN_KEY
#define SQLITE_CHANGESET_FOREIGN_KEY 5
#endif
#ifndef SQLITE_CHANGESET_OMIT
#define SQLITE_CHANGESET_OMIT 0
#endif
#ifndef SQLITE_CHANGESET_REPLACE
#define SQLITE_CHANGESET_REPLACE 1
#endif
#ifndef SQLITE_CHANGESET_ABORT
#define SQLITE_CHANGESET_ABORT 2
#endif

#ifndef SQLITE_CHANGESETSTART_INVERT
#define SQLITE_CHANGESETSTART_INVERT 0x0002
#endif
#ifndef SQLITE_CHANGESETAPPLY_NOSAVEPOINT
#define SQLITE_CHANGESETAPPLY_NOSAVEPOINT 0x0001
#endif
#ifndef SQLITE_CHANGESETAPPLY_INVERT
#define SQLITE_CHANGESETAPPLY_INVERT 0x0002
#endif
#ifndef SQLITE_CHANGESETAPPLY_IGNORENOOP
#define SQLITE_CHANGESETAPPLY_IGNORENOOP 0x0004
#endif
#ifndef SQLITE_CHANGESETAPPLY_FKNOACTION
#define SQLITE_CHANGESETAPPLY_FKNOACTION 0x0008
#endif
#ifndef SQLITE_CHANGESETAPPLY_NOUPDATELOOP
#define SQLITE_CHANGESETAPPLY_NOUPDATELOOP 0x0010
#endif

#ifndef SQLITE_SESSION_OBJCONFIG_SIZE
#define SQLITE_SESSION_OBJCONFIG_SIZE 1
#endif
#ifndef SQLITE_SESSION_OBJCONFIG_ROWID
#define SQLITE_SESSION_OBJCONFIG_ROWID 2
#endif
#ifndef SQLITE_SESSION_CONFIG_STRMSIZE
#define SQLITE_SESSION_CONFIG_STRMSIZE 1
#endif
#ifndef SQLITE_CHANGEGROUP_CONFIG_PATCHSET
#define SQLITE_CHANGEGROUP_CONFIG_PATCHSET 1
#endif

#ifdef __cplusplus
}
#endif

#endif // SQLITEX_SESSION_IN_HEADER

// ============================================================================
// C++ callback types and helpers for the streaming API
// ============================================================================

#ifdef __cplusplus

#include "sqlitex/concepts.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <istream>
#include <ostream>
#include <span>

namespace sqlitex {

/// Streaming input callback. Called by SQLite to request the next chunk
/// of changeset data.
///
/// @param buffer      Destination buffer span to fill (capacity =
/// buffer.size())
/// @param bytes_read  Set to the number of bytes written (0 = end of input).
/// @return 0 on success, non-zero to abort the operation.
using StreamInputFunc =
    std::function<int(std::span<std::byte> buffer, std::size_t& bytes_read)>;

/// Streaming output callback. Called by SQLite to deliver a chunk of
/// changeset data.
///
/// @param data  Span of bytes in the chunk.
/// @return 0 on success, non-zero to abort the operation.
using StreamOutputFunc = std::function<int(std::span<const std::byte> data)>;

/// Create a StreamInputFunc that reads from a std::istream.
/// The istream must remain valid for the duration of the streaming call.
[[nodiscard]] inline StreamInputFunc make_stream_input(std::istream& is) {
    return [&is](std::span<std::byte> buf, std::size_t& bytes_read) -> int {
        if (buf.empty()) {
            bytes_read = 0;
            return 1;
        }
        is.read(reinterpret_cast<char*>(buf.data()),
                static_cast<std::streamsize>(buf.size()));
        bytes_read = static_cast<std::size_t>(is.gcount());
        if (bytes_read == 0) {
            return 0;
        }
        return is.fail() && !is.eof() ? 1 : 0;
    };
}

/// Create a StreamOutputFunc that writes to a std::ostream.
/// The ostream must remain valid for the duration of the streaming call.
[[nodiscard]] inline StreamOutputFunc make_stream_output(std::ostream& os) {
    return [&os](std::span<const std::byte> data) -> int {
        if (!data.empty()) {
            os.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        }
        return os.fail() ? 1 : 0;
    };
}

/// Create a StreamInputFunc that reads from an in-memory byte span.
[[nodiscard]] inline StreamInputFunc make_stream_input(
    std::span<const std::byte> data) {
    return [remaining = data](std::span<std::byte> buf,
                              std::size_t& bytes_read) mutable -> int {
        if (remaining.empty()) {
            bytes_read = 0;
            return 0;
        }
        std::size_t const n =
            (buf.size() < remaining.size()) ? buf.size() : remaining.size();
        std::memcpy(buf.data(), remaining.data(), n);
        remaining = remaining.subspan(n);
        bytes_read = n;
        return 0;
    };
}

/// Create a StreamInputFunc from any contiguous byte range.
template <ConstByteRange R>
    requires(!std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
[[nodiscard]] inline StreamInputFunc make_stream_input(const R& data) {
    return make_stream_input(to_const_bytes(data));
}

namespace detail {

/// C-compatible shim for StreamOutputFunc.
inline int stream_output_shim(void* ctx, const void* data, int n) noexcept {
    try {
        auto& fn = *static_cast<StreamOutputFunc*>(ctx);
        std::span<const std::byte> span(
            static_cast<const std::byte*>(data),
            n > 0 ? static_cast<std::size_t>(n) : 0);
        return fn(span);
    } catch (...) {
        return SQLITE_ERROR;
    }
}

/// C-compatible shim for StreamInputFunc.
inline int stream_input_shim(void* ctx, void* data, int* pn) noexcept {
    try {
        auto& fn = *static_cast<StreamInputFunc*>(ctx);
        std::size_t const cap = (*pn > 0) ? static_cast<std::size_t>(*pn) : 0;
        std::size_t read = cap;
        int const rc =
            fn(std::span<std::byte>(static_cast<std::byte*>(data), cap), read);
        *pn = static_cast<int>(read);
        return rc;
    } catch (...) {
        return SQLITE_ERROR;
    }
}

} // namespace detail

} // namespace sqlitex

#endif // __cplusplus

#endif // SQLITEX_HAS_SESSION
