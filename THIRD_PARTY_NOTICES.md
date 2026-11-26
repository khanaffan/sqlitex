# Third-Party Notices

`sqlitex` itself is licensed under the MIT License (see [LICENSE](LICENSE)).
This repository bundles or optionally links against the following third-party
components:

## Bundled in This Repository

### SQLite (3.54.1)

- **Location:** `third_party/sqlite3/`
- **Upstream:** <https://sqlite.org/>
- **License:** Public Domain (<https://sqlite.org/copyright.html>)

All SQLite amalgamation source code (`sqlite3.c`, `sqlite3.h`) is dedicated to
the public domain by its authors.

### CloudSQLite / Block Cache VFS (bcVFS)

- **Location:** `third_party/bcvfs/`
- **Upstream:** <https://sqlite.org/cloudsqlite/> (check-in `843dff88c1`)
- **License:** Public Domain (maintained by the SQLite developers alongside
  SQLite; upstream `COPYING` is empty and no restrictive copyright notice is
  attached)

Local modifications applied to the bundled bcVFS sources:
- MSVC and OpenSSL 1.1 / 3.x portability fixes (`bcv_int.h`, `bcvmodule.c`,
  `bcvutil.c`, `blockcachevfs.c`).
- Re-entrancy safety fixes in `bcvfsUploadOneBlockTry` and `bcvfsPrefetchRun`
  (`blockcachevfs.c`) for custom `BcvContainer` storage modules that complete
  jobs synchronously without queuing HTTP requests.

## Optional External Dependencies (Not Vendored)

### libcurl

- **Used when:** `SQLITEX_BCVFS=ON`
- **Upstream:** <https://curl.se/>
- **License:** curl License (MIT/X-style permissive license)

### OpenSSL

- **Used when:** `SQLITEX_BCVFS=ON`
- **Upstream:** <https://www.openssl.org/>
- **License:** Apache License 2.0 (OpenSSL 3.x+)

### GoogleTest (v1.15.2)

- **Used when:** `SQLITEX_BUILD_TESTS=ON` (fetched via CMake `FetchContent`)
- **Upstream:** <https://github.com/google/googletest>
- **License:** BSD-3-Clause
