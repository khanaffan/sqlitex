# Changelog

All notable changes to `sqlitex` will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-11-26

### Added

- Modern **C++23** RAII wrapper around bundled **SQLite 3.54.1** with clean,
  `<sqlite3.h>`-free public headers.
- Type-safe `Flags<E>` bitmasks, C++20/23 concepts (`Bindable`, `ColumnValue`,
  `OwnedColumnValue`, `SqlInteger`, `ConstByteRange`), `std::span<const std::byte>`
  binary views, `<ranges>` typed row and changeset views, `std::expected<T, Error>`
  non-throwing entry points (`try_*`), `std::filesystem::path` overloads,
  `std::chrono` timeouts, `<=>` comparisons, and `std::format` specializations.
- Full coverage of core SQLite UTF-8 APIs: `Database`, `Transaction`,
  `Savepoint`, `Statement`, `Value` / `ValueRef`, `BlobIO`, `Backup`,
  `Snapshot`, custom SQL functions (scalar, aggregate, window), virtual tables,
  FTS5, R-Tree, CArray, hooks, WAL, and database serialization.
- Thread-safe bounded LRU `StatementCache` and `Database::prepare_cached()` with
  RAII `CachedStatement` leases and automatic `ScopedPolicy` bypass.
- `ScopedPolicy` RAII authorizer and connection-hardening guard.
- SQLite Session extension wrappers (`Session`, `Changeset`, `ChangesetIterator`,
  `Changegroup`, `ChangeBuilder`, `Rebaser`).
- CloudSQLite / bcVFS wrappers (`Bcvfs`, `PrefetchHandle`, `BcvClient`,
  `BcvContainer`, `BcvJob`, `BcvRequest`) bundled from upstream check-in
  `843dff88c1`, plus `blockcachevfsd` daemon target.
- CMake install/export support (`find_package(sqlitex CONFIG REQUIRED)`),
  DLL-safe public headers (`SQLITEX_EXPORT`), `CMakePresets.json`, MIT license,
  and vcpkg overlay port (`ports/sqlitex/`).
- Standalone examples (`basic_usage`, `statement_cache`, `scoped_policy`,
  `session_tracking`, `changeset_apply`, `changegroup_merge`, `bcvfs_basic`) and
  comprehensive Google Test suite.

### Fixed

- Re-entrancy use-after-free / out-of-bounds read in CloudSQLite
  `bcvfsUploadOneBlockTry` and `nOutstanding` underflow in `bcvfsPrefetchRun`
  when custom `BcvContainer` modules complete synchronously without queuing
  HTTP requests.
- Enabled Winsock2 loopback HTTP server support in `CloudSqliteOfflineTest` so
  Windows exercises both HTTP-backed and synchronous `BcvContainer` paths.

[Unreleased]: https://github.com/khanaffan/sqlitex/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/khanaffan/sqlitex/releases/tag/v0.1.0
