# sqlitex

A modern C++17 RAII wrapper for **SQLite 3.54.1** and **CloudSQLite (bcVFS)**,
with full coverage of the modern UTF-8 SQLite public API, the Session extension,
built-in extensions (FTS5, R-Tree, CArray), scoped authorization policies, and
cloud-backed storage.

## Highlights

- **Bundled SQLite 3.54.1** — compiled from source as part of the library with
  Session, Preupdate Hook, FTS5, R-Tree, JSON1, CArray, Snapshot, Scanstatus,
  Normalize, Column Metadata, DBSTAT, Unlock Notify, and Setlk Timeout enabled.
- **Clean Public Headers** — no public header includes `<sqlite3.h>`; forward
  declarations and constants live in `sqlitex/detail/sqlite3_fwd.h` and
  `sqlitex/detail/session_api.h`.
- **Move-Only RAII & Typed Exceptions** — every handle (`Database`, `Statement`,
  `BlobIO`, `Backup`, `Snapshot`, `Session`, `Changegroup`, `Rebaser`, `Bcvfs`,
  `PrefetchHandle`, `BcvClient`) is move-only and throws typed `sqlitex::Error`
  subclasses on failure.
- **Complete CloudSQLite Support** — wraps `blockcachevfs.h` (`Bcvfs`,
  `PrefetchHandle`), `bcvutil.h` (`BcvClient`), and `bcvmodule.h`
  (`BcvContainer`, `BcvJob`, `BcvRequest`), plus builds the `blockcachevfsd`
  daemon.

## Quick Start

```cpp
#include <sqlitex/sqlitex.h>
#include <iostream>

int main() {
    sqlitex::Database db(":memory:");
    db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");

    {
        sqlitex::Database::Transaction tx(db);
        auto stmt = db.prepare("INSERT INTO users VALUES (?, ?)");
        stmt.bind_all(1, std::string_view("Alice")).execute();
        stmt.reset();
        stmt.bind_all(2, std::string_view("Bob")).execute();
    } // commits automatically on normal scope exit; rolls back on exception

    for (const auto& row : db.query("SELECT id, name FROM users ORDER BY id")) {
        std::cout << row[0].as_int64() << ": " << row[1].as_text() << '\n';
    }
}
```

## Requirements

- **C++17** compiler (Clang 7+, GCC 8+, MSVC 2019+)
- **CMake** 3.15+
- **libcurl** and **OpenSSL** (only when `SQLITEX_BCVFS=ON`)

No system SQLite installation is needed.

## Building

### macOS

```bash
brew install curl openssl # optional; required only when SQLITEX_BCVFS=ON
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Ubuntu / Debian)

```bash
sudo apt-get install libcurl4-openssl-dev libssl-dev # optional for bcVFS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows

```powershell
# Optional for bcVFS: vcpkg install curl openssl
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `SQLITEX_SESSION` | `ON` | Enable Session / Changeset / Changegroup / Rebaser wrappers (`SQLITEX_HAS_SESSION`) |
| `SQLITEX_BCVFS` | `ON` | Enable CloudSQLite / bcVFS wrappers and `blockcachevfsd` (`SQLITEX_HAS_BCVFS`) |
| `SQLITEX_BCVFS_SOURCE_DIR` | *(bundled)* | Override path to CloudSQLite C sources (default: `third_party/bcvfs/`) |
| `SQLITEX_BUILD_TESTS` | `ON` | Build Google Test suite (fetches Google Test v1.15.2 via `FetchContent`) |
| `SQLITEX_BUILD_EXAMPLES` | `ON` | Build example programs |
| `SQLITEX_LINT` | `OFF` | Run `clang-tidy` during compilation |
| `BUILD_SHARED_LIBS` | `OFF` | Build `sqlitex` as a shared library (DLL on Windows via `sqlitex_export.h`) |

When `SQLITEX_BCVFS=OFF`, the CloudSQLite headers remain includable and linkable
as safe stubs that throw `sqlitex::Error` when invoked, so downstream code can
compile without conditional `#ifdef` boilerplate unless desired.

### Running Tests, Examples & Formatting

```bash
# Run unit & offline integration tests
ctest --test-dir build --output-on-failure

# Run examples
./build/examples/basic_usage
./build/examples/session_tracking
./build/examples/changeset_apply
./build/examples/changegroup_merge
# ./build/examples/bcvfs_basic   # requires cloud storage credentials

# Format / check formatting (when clang-format is installed)
cmake --build build --target clang-format
cmake --build build --target clang-format-check
```

Note: CloudSQLite live integration tests (`BcvfsIntegrationTest.*`) auto-skip
when Azurite is unavailable; the offline end-to-end CloudSQLite suite
(`CloudSqliteOfflineTest.*`) runs without external services using a custom
`BcvContainer` module over loopback HTTP.

## Project Structure

```
include/sqlitex/
  sqlitex.h              Umbrella header
  error.h                ErrorCode enum & typed exception hierarchy
  sqlite.h               Global SQLite runtime, config, memory, VFS, URI & keywords
  value.h                Owned Value & non-owning ValueRef
  database.h             Database, Transaction, Savepoint, hooks, WAL, serialize
  statement.h            Prepared Statement, bind helpers, scanstatus, metadata
  backup.h               Online Backup API
  snapshot.h             WAL Snapshot API
  blobio.h               Incremental BLOB I/O
  policy.h               ScopedPolicy authorizer & connection hardening guard
  function.h             Scalar, raw, aggregate, and window SQL functions
  virtual_table.h        VirtualTable, Cursor, IndexInfo, VtabInIterator
  fts5.h                 FTS5 custom tokenizers & auxiliary functions
  rtree.h                R-Tree geometry & scored query callbacks
  session.h              Session change tracking & config
  changeset.h            Changeset & Patchset (apply, invert, concat, streaming)
  changeset_iterator.hπ   ChangesetIterator (memory & streaming)
  changegroup.h          Changegroup merge & programmatic change builder
  rebaser.h              Changeset Rebaser
  bcvfs.h                CloudSQLite VFS (blockcachevfs.h)
  prefetch.h             CloudSQLite PrefetchHandle
  bcv_util.h             CloudSQLite BcvClient (bcvutil.h)
  bcv_module.h           Custom CloudSQLite storage modules (bcvmodule.h)
  detail/                <sqlite3.h>-free forward declarations & internal helpers
src/                     Library implementation
third_party/
  sqlite3/               SQLite 3.54.1 amalgamation
  bcvfs/                 CloudSQLite sources (check-in 997acb171b + local portability fixes)
tests/                   Google Test suite
examples/                Standalone example programs
```

## API Guide

Include `<sqlitex/sqlitex.h>` for everything, or include individual headers as
needed.

### Database, Statements & Values

```cpp
sqlitex::Database db("app.db");

// Prepare with SQLITE_PREPARE_* flags via prepare_v3
auto stmt = db.prepare_v3("INSERT INTO items (id, name, payload) VALUES (?, ?, ?)",
                          static_cast<unsigned int>(sqlitex::PrepareFlag::Persistent));
stmt.bind(1, 42)
    .bind(2, std::string_view("widget"))
    .bind_zeroblob(3, 1024)
    .execute();

// Bind a C++ vector via SQLite's built-in carray extension
std::vector<int64_t> ids = {1, 2, 42};
auto q = db.prepare("SELECT name FROM items WHERE id IN (SELECT value FROM carray(?))");
q.bind_carray(1, ids);
while (q.step()) {
    std::string_view name = q.column_text(0);
    // Also available: column_value(), column_raw_value(), column_decltype(),
    // column_database_name(), column_table_name(), column_origin_name(),
    // expanded_sql(), normalized_sql(), status(), scanstatus(), set_explain().
}

// Convenience single-value query
auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM items");
```

Nested transactions are supported via `Database::Savepoint`:

```cpp
sqlitex::Database::Transaction tx(db);
db.execute("INSERT INTO items (id, name) VALUES (1, 'outer')");
{
    sqlitex::Database::Savepoint sp(db, "inner");
    db.execute("INSERT INTO items (id, name) VALUES (2, 'discarded')");
    sp.rollback(); // or let an exception roll back automatically
}
```

Additional `Database` capabilities:
- **Serialization**: `serialize()` returns an RAII `SerializedDatabase`;
  `deserialize()` loads an image into a schema.
- **WAL control**: `wal_checkpoint()`, `wal_checkpoint_v2()` (returning
  `CheckpointResult`), `wal_autocheckpoint()`, `set_wal_hook()`.
- **Hooks & handlers**: `set_update_hook()`, `set_preupdate_hook()` (with
  `PreupdateContext`), `set_commit_hook()`, `set_rollback_hook()`,
  `set_trace_hook()`, `set_progress_handler()`, `set_busy_handler()`,
  `set_autovacuum_pages()`, `unlock_notify()`, `setlk_timeout()`.
- **Collations & extensions**: `create_collation()`, `drop_collation()`,
  `set_collation_needed()`, `enable_load_extension()`, `load_extension()`.
- **Config, limits & introspection**: `db_config()`, `db_config_get()`,
  `set_main_db_name()`, `set_lookaside()`, `set_fp_digits()`, `limit()`,
  `db_status()`, `txn_state()`, `db_readonly()`, `db_filename()`, `db_name()`,
  `db_names()`, `table_column_metadata()`, `table_exists()`, `set_clientdata()`,
  `get_clientdata()`, `db_cacheflush()`, `db_release_memory()`, `next_stmt()`,
  `active_statements()`, `file_control()`, `interrupt()`, `error_offset()`,
  `system_errno()`.

### Incremental BLOB I/O, Online Backup & WAL Snapshots

```cpp
// Incremental BLOB I/O (blobio.h)
sqlitex::BlobIO blob(db, "main", "items", "payload", 42, /*readwrite=*/true);
blob.write("hello", 5, 0);
std::vector<uint8_t> bytes = blob.read_all();
blob.reopen(43);

// Online Backup (backup.h)
sqlitex::Database dest("backup.db");
sqlitex::Backup backup(dest, db);
backup.run(/*pages_per_step=*/64, [](int remaining, int total) {
    // optional progress callback
});

// WAL Snapshots (snapshot.h)
db.execute("PRAGMA journal_mode=WAL");
db.execute("BEGIN");
sqlitex::Snapshot snap = sqlitex::Snapshot::get(db);
db.execute("COMMIT");
// Later, open a read transaction at the historical point:
snap.open(other_conn);
```

### Global SQLite Runtime (`sqlite.h`)

Free functions in `namespace sqlitex` wrap process-wide SQLite APIs:
- Version & metadata: `libversion()`, `libversion_number()`, `sourceid()`,
  `threadsafe()`, `compileoption_used()`, `compileoption_get()`,
  `compile_options()`, `errstr()`.
- Lifecycle & configuration: `initialize()`, `shutdown()`,
  `enable_shared_cache()`, `log()`, `config_threading_mode()`,
  `config_memstatus()`, `config_lookaside()`, `config_uri()`,
  `config_covering_index_scan()`, `config_mmap_size()`, `config_pmasz()`,
  `config_stmtjrnl_spill()`, `config_small_malloc()`,
  `config_sorterref_size()`, `config_memdb_maxsize()`,
  `config_rowid_in_view()`, `config_pcache_hdrsz()`, `config_log()`.
- Memory & status: `memory_used()`, `memory_highwater()`, `soft_heap_limit()`,
  `hard_heap_limit()`, `release_memory()`, `status(StatusOp)`.
- SQL & keywords: `complete()`, `incomplete()`, `keyword_count()`,
  `keyword_name()`, `keyword_check()`.
- URI & filenames: RAII `UriFilename`, `uri_parameter()`, `uri_boolean()`,
  `uri_int64()`, `uri_key()`, `filename_database()`, `filename_journal()`,
  `filename_wal()`, `database_file_object()`.
- Registries: `vfs_find()`, `vfs_register()`, `vfs_unregister()`,
  `auto_extension()`, `cancel_auto_extension()`, `reset_auto_extension()`.

### Custom SQL Functions, Virtual Tables, FTS5 & R-Tree

- **SQL Functions (`function.h`)**:
  - `db.create_function()` (owned `Value` args) and `db.create_raw_function()`
    (non-owning `ValueRef` args supporting subtypes, `bind_pointer` /
    `set_result_pointer`, `vtab_nochange`, `auxdata`, `set_result_zeroblob`).
  - `db.create_aggregate()` (class-based `AggregateFunction` or typed-state
    lambda overload), `db.create_window_function()` (`WindowFunction` with
    `step`/`final`/`value`/`inverse`), `db.drop_function()`, and
    `db.overload_function()`.
- **Virtual Tables (`virtual_table.h`)**:
  - Subclass `VirtualTable` and `Cursor` and register with `db.create_module()`
    (returns RAII `ModuleRegistration`) or prune with `db.drop_modules()`.
  - `IndexInfo` wraps constraints, `order_by`, `col_used`, `collation()`,
    `distinct()`, `set_in()`, and `rhs_value()`.
  - `VtabInIterator` iterates `IN (...)` RHS lists via `sqlite3_vtab_in_first` /
    `sqlite3_vtab_in_next`.
- **FTS5 (`fts5.h`)**:
  - Obtain `Fts5Api::from_database(db)` to register custom `Fts5Tokenizer`
    implementations (`create_tokenizer`, `has_tokenizer`, `tokenize_with`) and
    auxiliary ranking/snippet functions (`create_function` receiving
    `Fts5ExtensionContext`).
- **R-Tree (`rtree.h`)**:
  - Register 1st-gen geometry callbacks with `register_rtree_geometry_callback()`
    and 2nd-gen scored spatial query callbacks with
    `register_rtree_query_callback()` (receiving `RTreeQueryInfo` and
    `RTreeWithin`).

### Session Extension, Changesets, Changegroups & Rebaser

Available when `SQLITEX_SESSION=ON`:

```cpp
// Capture changes
sqlitex::Session session(db, "main");
int enable_size = 1;
session.set_object_config(SQLITE_SESSION_OBJCONFIG_SIZE, &enable_size);
session.attach_all(); // or session.attach("users") / session.set_table_filter(...)

// ... modify tables ...
sqlitex::Changeset cs = session.changeset(); // or session.patchset() / session.diff(...)

// Iterate changes
auto it = cs.begin();
while (it.next()) {
    auto op = it.op();
    auto table = it.table_name();
}

// Apply with conflict handler and capture rebase metadata (apply_v2 / apply_v3)
std::vector<uint8_t> rebase_data;
cs.apply_v2(
    target_db,
    [](sqlitex::ConflictType type, const sqlitex::ChangesetIterator& cur) {
        return sqlitex::ChangesetAction::Omit;
    },
    /*flags=*/0,
    &rebase_data);

// Rebase local changes against conflict resolutions
sqlitex::Rebaser rebaser;
rebaser.configure(rebase_data);
sqlitex::Changeset rebased = rebaser.rebase(local_cs);
```

Additional Session capabilities:
- **Streaming**: `Session::changeset_stream()`, `patchset_stream()`,
  `changeset_to_stream()`, `patchset_to_stream()`,
  `Session::set_stream_chunk_size()`, `Changeset::apply_stream()`,
  `apply_v3_stream()`, `invert_stream()`, `concat_stream()`,
  `ChangesetIterator::from_stream()`, `Changegroup::add_stream()`,
  `output_stream()`, and `Rebaser::rebase_stream()`.
- **Changegroup**: merge multiple changesets/patchsets with `add()`,
  `add_change()`, `set_patchset_mode()`, `set_schema()`, or construct changes
  programmatically via `change_begin()` / `change_*()` / `change_finish()`.

### Scoped Authorization (`ScopedPolicy`)

`sqlitex::ScopedPolicy` installs an SQLite authorizer for one C++ scope.
Constraints are added while the guard is alive; its destructor removes them and
restores the previous policy and any hardening settings it changed.

```cpp
{
    sqlitex::ScopedPolicy policy(db);
    policy.read_only()
        .deny_table("audit_log")
        .deny_column("users", "password_hash")
        .defensive()
        .trusted_schema(false);

    // Custom rule. Ignore is honored only for read-side redaction under
    // read_only(): ssn reads as NULL.
    policy.add([](const sqlitex::AuthorizerRequest& r) {
        using sqlitex::AuthorizerAction;
        using sqlitex::AuthorizerResult;
        return r.action == AuthorizerAction::Read && r.arg1 == "users" &&
                       r.arg2 == "ssn"
                   ? AuthorizerResult::Ignore
                   : AuthorizerResult::Allow;
    });

    auto rows = db.query("SELECT name, ssn FROM users");
    // db.execute("DELETE FROM users"); // throws sqlitex::AuthError
} // previous authorizer and settings restored here
```

Constraints and hardening:
- `add(callback)`: custom rule; callbacks run after built-ins, first `Deny`
  wins. A thrown exception denies the request and is rethrown from the sqlitex
  call that compiled the SQL.
- `deny(action)` and one `deny_*()` helper per `AuthorizerAction`.
- `deny_table(name, schema)`, `deny_column(table, column, schema)`,
  `deny_function(name)`, `deny_pragma(name)` (case-insensitive; empty schema
  means any).
- `read_only()`: permits queries, transactions, savepoints, and introspection
  pragmas (`table_info`, `table_xinfo`, `index_list`, `index_info`,
  `index_xinfo`, `foreign_key_list`, `data_version`); everything else is denied.
- `allow_only({actions...})`: denies unlisted actions; repeated calls intersect.
- Hardening (innermost guard only, restored LIFO): `defensive()`,
  `trusted_schema()`, `triggers_enabled()`, `views_enabled()`,
  `attach_create()`, `attach_write()`, `max_attached()`.

Semantics and limits:
- Nested guards replace the outer policy until they end; hardening settings are
  layered so inner guards inherit current values.
- Guards must be destroyed in LIFO order, and the `Database` must not be closed,
  moved, or destroyed while a guard is alive (debug assertions).
- Built-in transaction rules deny only `BEGIN` and `SAVEPOINT` creation, so open
  transactions can always commit, roll back, or release.
- `Ignore` from a custom rule becomes `Deny` unless the action is `Read`,
  `Function`, `Select`, or `Recursive` under `read_only()`, and never applies
  inside Session/Changeset operations.
- Helpers also deny SQLite's internal requests (e.g. `deny_insert()` blocks
  `CREATE TABLE`, `deny_reindex()` blocks `CREATE INDEX`, `deny_attach()` blocks
  `VACUUM`).
- `BlobIO` opens are checked as `Read` (plus `Update` when writable);
  `BlobIO::reopen()` and pre-existing handles are not.
- Authorization occurs at SQL compile time (including automatic re-prepare
  during `step()`). Every scope change expires prepared statements.
- SQLite does not authorize schema loading, `CHECK` constraints, `VIRTUAL`
  generated columns, or expression/partial index expressions at use time; pair
  with `trusted_schema(false)` to restrict functions there.
- Do not call `sqlite3_set_authorizer` directly on `handle()` while guards are
  active.

### CloudSQLite / bcVFS

Bundled from the [SQLite CloudSQLite](https://sqlite.org/cloudsqlite/)
repository (check-in `997acb171b`, with local portability fixes for MSVC and
older OpenSSL).

#### Block Cache VFS & Prefetch (`bcvfs.h`, `prefetch.h`)

```cpp
#include <filesystem>

auto cache = std::filesystem::temp_directory_path() / "sqlite-cache";
sqlitex::Bcvfs bcvfs(cache.string(), "bcvfs");
bcvfs.set_cache_size(256LL * 1024 * 1024)
     .set_max_requests(8)
     .set_http_timeout(std::chrono::seconds(60))
     .set_http_log_timeout(std::chrono::seconds(300))
     .set_http_log_max_entries(1000);

bcvfs.set_auth_callback([](std::string_view storage, std::string_view account,
                           std::string_view container, std::string& token) {
    token = "..."; // OAuth or SAS token
    return true;
});

bcvfs.attach("azure", "myaccount", "mycontainer", "mydata");

// Optional background prefetching
sqlitex::PrefetchHandle pf = bcvfs.prefetch("mydata", "app.db");
while (!pf.step(/*max_requests=*/4, std::chrono::milliseconds(500))) {
    // inspect pf.outstanding_requests() / pf.on_demand_blocks()
}

// Open database through the bcVFS VFS
sqlitex::Database db("/mydata/app.db",
                     sqlitex::OpenFlag::ReadWrite | sqlitex::OpenFlag::Create,
                     "bcvfs");
bcvfs.register_vtab(db); // exposes bcv_container, bcv_http_log, etc.

// Sync with cloud storage
bcvfs.upload("mydata");
bcvfs.poll("mydata");
// Also available: bcvfs.copy(), bcvfs.remove(), bcvfs.revert(), bcvfs.is_daemon()
```

#### Container Management Client (`bcv_util.h`)

`BcvClient` wraps `sqlite3_bcv_*` for administrative operations without mounting
a VFS:

```cpp
sqlitex::BcvClient client("azure", "myaccount", "myauth", "mycontainer");
client.set_max_requests(4)
      .set_http_timeout(std::chrono::seconds(60))
      .set_progress_callback([](int64_t done, int64_t total) { return 0; });

client.create();
client.upload("local.db", "remote.db");
client.copy("remote.db", "backup.db");
client.download("remote.db", "restored.db");
client.remove("backup.db");
client.cleanup(/*age_seconds=*/3600);
```

#### Custom Storage Modules (`bcv_module.h`)

Implement `BcvContainer` (`fetch`, `put`, `remove`, `list`, `create_container`,
`destroy_container`) using `BcvJob` (`create_request`, `set_result`,
`add_list_entry`, `set_headers`, `set_etag`, `set_error`, `prefix`) and
`BcvRequest` (`set_method`, `set_uri`, `set_log`, `set_header`, `set_body`,
`status`, `header`, `raw_headers`, `body`), then register with
`register_bcv_module("mybackend", factory)` and tear down with `bcv_shutdown()`.

## Error Handling

All failures throw `sqlitex::Error` or a specific subclass mapped from the
primary SQLite result code (`RuntimeError`, `PermissionDenied`, `AbortError`,
`BusyError`, `IoError`, `CorruptError`, `NotFoundError`, `ConstraintError`,
`MismatchError`, `MisuseError`, `AuthError`, `RangeError`, `NotADbError`,
`InternalError`, `ProtocolError`):

```cpp
try {
    db.execute("INVALID SQL");
} catch (const sqlitex::ConstraintError& e) {
    std::cerr << "Constraint (" << e.extended_code() << "): " << e.what() << '\n';
} catch (const sqlitex::Error& e) {
    std::cerr << "SQLite error (" << e.code() << "): " << e.what() << '\n';
}
```

## Thread Safety

SQLite is compiled with `SQLITE_THREADSAFE=1` (serialized mode). `sqlitex` does
not add additional locking around individual C++ wrapper objects: share a single
`Database` across threads only according to SQLite's threading rules, or open
one `Database` per thread.

## License

MIT
