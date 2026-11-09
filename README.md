# Sqlitex — C++ Wrapper for SQLite with bcVFS & Session Extension

A modern C++17 RAII wrapper for SQLite, including:

- **Core SQLite** — `Database`, `Statement`, and `Value` classes
- **Cloud Backed SQLite (bcvfs)** — Block Cache Virtual File System for cloud storage
- **Session Extension** — Changeset capture, iteration, application, and merging
- **Streaming API** — Callback-based streaming for large changesets

## Requirements

- **C++17** compiler (Clang 7+, GCC 8+, MSVC 2019+)
- **CMake** 3.15+
- **curl** and **OpenSSL** (required for bcVFS support)

SQLite3 is bundled as source (amalgamation) and compiled as part of the library.
No external SQLite installation is needed. Session extension support is always enabled.

## Quick Start

```cpp
#include <sqlitex/sqlitex.h>

int main() {
    sqlitex::Database db(":memory:");

    db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");

    auto stmt = db.prepare("INSERT INTO users VALUES (?, ?)");
    stmt.bind_all(1, std::string_view("Alice"));
    stmt.execute();

    for (const auto& row : db.query("SELECT * FROM users")) {
        std::cout << row[1].as_text() << '\n';
    }
}
```

## Building

### macOS

```bash
# Install dependencies for bcVFS (optional)
brew install curl openssl

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux (Ubuntu/Debian)

```bash
# Install dependencies for bcVFS (optional)
sudo apt-get install libcurl4-openssl-dev libssl-dev

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Windows

```bash
# Install curl and OpenSSL via vcpkg
# vcpkg install curl openssl

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `SQLITEX_SESSION` | `ON` | Build with session/changeset support |
| `SQLITEX_BCVFS` | `ON` | Build with Block Cache VFS support |
| `SQLITEX_BUILD_TESTS` | `ON` | Build test suite (fetches Google Test) |
| `SQLITEX_BUILD_EXAMPLES` | `ON` | Build example programs |
| `SQLITEX_LINT` | `OFF` | Enable clang-tidy static analysis |
| `BUILD_SHARED_LIBS` | `OFF` | Build as shared library (DLL on Windows) |

### With bcVFS support

bcVFS source files are bundled in `third_party/bcvfs/` (fetched from the
[SQLite CloudSQLite repository](https://sqlite.org/cloudsqlite/), check-in
`843dff88c1`, with small local portability patches for MSVC and older OpenSSL).
They are found automatically. To override:

```bash
cmake .. -DSQLITEX_BCVFS_SOURCE_DIR=/path/to/bcvfs/sources
```

To disable bcVFS:

```bash
cmake .. -DSQLITEX_BCVFS=OFF
```

### Without session extension

```bash
cmake .. -DSQLITEX_SESSION=OFF
```

### Running examples

```bash
cmake .. -DSQLITEX_BUILD_EXAMPLES=ON
cmake --build build

./build/examples/basic_usage
./build/examples/session_tracking
./build/examples/changeset_apply
./build/examples/changegroup_merge
```

### Running tests

```bash
cmake .. -DSQLITEX_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Project Structure

```
include/sqlitex/     Public headers
src/                 C++ library source
third_party/
  sqlite3/           SQLite amalgamation (3.54.1)
  bcvfs/             bcVFS C sources (CloudSQLite check-in 843dff88c1)
tests/               Google Test suite
examples/            Example programs
```

## API Overview

### Database (connection)

```cpp
sqlitex::Database db("path/to/db.sqlite");
// or with a custom VFS:
sqlitex::Database db("/alias/db", sqlitex::OpenFlag::ReadWrite, "bcvfs");

db.execute("CREATE TABLE ...");
auto stmt = db.prepare("SELECT ... WHERE id = ?");
auto rows = db.query("SELECT * FROM t");
auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
```

### Statement (prepared statement)

```cpp
auto stmt = db.prepare("INSERT INTO t VALUES (?, ?, ?)");
stmt.bind(1, 42);
stmt.bind(2, std::string_view("hello"));
stmt.bind(3, nullptr);
stmt.execute();

// Or chained:
stmt.reset();
stmt.bind_all(1, "text", 3.14).execute();

// Reading results:
auto stmt = db.prepare("SELECT * FROM t");
while (stmt.step()) {
    int64_t id = stmt.column_int64(0);
    std::string_view name = stmt.column_text(1);
}
```

### Session (change tracking)

```cpp
sqlitex::Session session(db, "main");
session.attach_all();  // Track all tables

// ... application makes changes ...

sqlitex::Changeset cs = session.changeset();
// cs can be stored, transmitted, or applied to another database
```

### Changeset (iteration & application)

```cpp
// Iterate over changes
auto iter = cs.begin();
while (iter.next()) {
    auto op = iter.op();            // Insert, Delete, or Update
    auto table = iter.table_name();
    auto old_val = iter.old_int(0);
    auto new_val = iter.new_int(0);
}

// Apply to another database
cs.apply(target_db,
    [](sqlitex::ConflictType type, const sqlitex::ChangesetIterator& it) {
        return sqlitex::ChangesetAction::Omit;  // Skip conflicts
    });

// Undo via inversion
sqlitex::Changeset undo = cs.invert();
undo.apply(db, handler);
```

### Streaming API (for large changesets)

```cpp
// Stream a changeset from a session to an ostream
sqlitex::Session session(db);
session.enable(true);
session.attach_all();
// ... make changes ...
session.changeset_to_stream(std::cout);

// Stream changeset via callback
std::vector<uint8_t> buffer;
session.changeset_stream([&](const void* data, int size) {
    auto* bytes = static_cast<const uint8_t*>(data);
    buffer.insert(buffer.end(), bytes, bytes + size);
    return 0; // return non-zero to abort
});

// Stream-apply a changeset
auto input = make_stream_input(changeset_data, changeset_size);
Changeset::apply_stream(target_db, std::move(input), conflict_handler);

// Stream-add to a changegroup
sqlitex::Changegroup group;
group.add_stream(std::move(input));

// Stream-output from a changegroup
group.output_stream([](const void* data, int size) {
    // process chunk
    return 0;
});

// Stream-iterate
auto iter = ChangesetIterator::from_stream(std::move(input));
while (iter.next()) {
    // ... inspect changes ...
}
```

### Changegroup (merge)

```cpp
sqlitex::Changegroup group;
group.add(cs1);
group.add(cs2);
sqlitex::Changeset merged = group.output();
merged.apply(target, handler);
```

### bcvfs (cloud-backed storage)

```cpp
#include <filesystem>

// Create and configure the VFS
auto cache = std::filesystem::temp_directory_path() / "sqlite-cache";
sqlitex::Bcvfs bcvfs(cache.string().c_str(), "bcvfs");
bcvfs.set_cache_size(256 * 1024 * 1024)
     .set_max_requests(8);

// Set up authentication
bcvfs.set_auth_callback([](auto storage, auto account, auto container,
                            std::string& token) {
    token = "...";  // OAuth or SAS token
    return true;
});

// Attach a cloud container
bcvfs.attach("azure", "myaccount", "mycontainer", "mydata");

// Open database (uses the bcvfs VFS)
sqlitex::Database db("/mydata/mydb.db",
    sqlitex::OpenFlag::ReadWrite | sqlitex::OpenFlag::Create,
    "bcvfs");

// ... normal SQL operations ...

// Upload changes to cloud
bcvfs.upload("mydata");

// Introspection
bcvfs.register_vtab(db);
auto containers = db.query("SELECT * FROM bcv_container");
```

### ScopedPolicy (scoped authorization)

`sqlitex::ScopedPolicy` installs an SQLite authorizer for one C++ scope.
Constraints are added while the guard is alive; its destructor removes them
and restores the previous policy and any hardening settings it changed.

```cpp
{
    sqlitex::ScopedPolicy policy(db);
    policy.read_only()
        .deny_table("audit_log")
        .deny_column("users", "password_hash")
        .defensive()
        .trusted_schema(false);

    // Custom rule.  Ignore is honored only for read-side redaction under
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
    // db.execute("DELETE FROM users");  // throws sqlitex::AuthError
}   // previous authorizer and settings restored here
```

Constraints:

- `add(callback)`: custom rule; callbacks run after built-ins, the first
  `Deny` wins. A thrown exception denies the request and is rethrown from the
  sqlitex call that compiled the SQL.
- `deny(action)` and one `deny_*()` helper per `AuthorizerAction`.
- `deny_table(name, schema)`, `deny_column(table, column, schema)`,
  `deny_function(name)`, `deny_pragma(name)` (case-insensitive; empty schema
  means any).
- `read_only()`: queries, transactions, savepoints, and the introspection
  pragmas `table_info`, `table_xinfo`, `index_list`, `index_info`,
  `index_xinfo`, `foreign_key_list`, `data_version`. Everything else is denied.
- `allow_only({actions...})`: deny every unlisted action; calls intersect.
- Hardening (innermost guard only, restored LIFO): `defensive()`,
  `trusted_schema()`, `triggers_enabled()`, `views_enabled()`,
  `attach_create()`, `attach_write()`, `max_attached()`.

Semantics and limits:

- Nested guards replace the outer policy until they end. Hardening settings
  are layered instead: inner guards inherit the current values.
- Guards must be destroyed in LIFO order, and the `Database` must not be
  closed, moved, or destroyed while a guard is alive (debug assertions).
- Built-in transaction rules deny only `BEGIN` and `SAVEPOINT` creation, so
  open transactions can always commit, roll back, or release.
- `Ignore` from a custom rule becomes `Deny` unless the action is Read,
  Function, Select, or Recursive under `read_only()`, and never applies inside
  Session/Changeset operations (redacted values would corrupt changesets).
- Helpers are strict and also deny SQLite's internal requests: for example
  `deny_insert()` blocks `CREATE TABLE`, `deny_reindex()` blocks
  `CREATE INDEX`, and `deny_attach()` blocks `VACUUM`.
- `BlobIO` opens are checked as Read (plus Update when writable);
  `BlobIO::reopen` and handles opened before the scope are not.
- Authorization happens when SQL is compiled, including automatic re-prepare
  during `step()`. Every scope change expires prepared statements.
- SQLite does not authorize schema loading, CHECK constraints, VIRTUAL
  generated columns, or expression/partial index expressions at use time; use
  `trusted_schema(false)` to restrict functions there.
- Raw `handle()` users see SQLite's codes; do not call
  `sqlite3_set_authorizer` directly while guards are in use.

## Error Handling

All errors are thrown as typed exceptions:

```cpp
try {
    db.execute("INVALID SQL");
} catch (const sqlitex::RuntimeError& e) {
    std::cerr << "SQL error: " << e.what() << '\n';
    std::cerr << "Code: " << e.code() << '\n';
} catch (const sqlitex::ConstraintError& e) {
    std::cerr << "Constraint violation: " << e.what() << '\n';
} catch (const sqlitex::Error& e) {
    std::cerr << "Other error: " << e.what() << '\n';
}
```

## Thread Safety

This library does not add thread safety beyond what SQLite provides. Each
`Database` object should be used from a single thread at a time, or with
SQLite's serialized threading mode.

## License

MIT
