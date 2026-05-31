# CloudSQLite — C++ Wrapper for SQLite with bcvfs & Session Extension

A modern C++17 RAII wrapper for SQLite, including:

- **Core SQLite** — `Database`, `Statement`, and `Value` classes
- **Cloud Backed SQLite (bcvfs)** — Block Cache Virtual File System for cloud storage
- **Session Extension** — Changeset capture, iteration, application, and merging

## Requirements

- **C++17** compiler (Clang 7+, GCC 8+, MSVC 2019+)
- **CMake** 3.15+
- **SQLite** 3.17.0+ with `SQLITE_ENABLE_SESSION` and `SQLITE_ENABLE_PREUPDATE_HOOK`
  - Homebrew: `brew install sqlite` (session extension is enabled)
  - Ubuntu/Debian: `apt install libsqlite3-dev` (check for session support)
- **bcvfs** (optional): `blockcachevfs.h` and `blockcachevfs.c` from the [SQLite source tree](https://sqlite.org/cloudsqlite/doc/trunk/src/)

## Quick Start

```cpp
#include <cloudsqlite/cloudsqlite.h>

int main() {
    cloudsqlite::Database db(":memory:");

    db.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");

    auto stmt = db.prepare("INSERT INTO users VALUES (?, ?)");
    stmt.bind_all(1, std::string_view("Alice"));
    stmt.execute();

    for (const auto& row : db.query("SELECT * FROM users")) {
        // row[0] is id, row[1] is name
        std::cout << row[1].as_text() << '\n';
    }
}
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### With bcvfs support

Download `blockcachevfs.h` and `blockcachevfs.c` from the SQLite source tree and place them in `src/`:

```bash
cp blockcachevfs.h blockcachevfs.c src/
mkdir build && cd build
cmake .. -DCLOUDSQLITE_BCVFS=ON
make -j$(nproc)
```

Or set the source directory:

```bash
cmake .. -DCLOUDSQLITE_BCVFS_SOURCE_DIR=/path/to/sqlite/ext/bcvfs
```

### Running examples

```bash
cmake .. -DCLOUDSQLITE_BUILD_EXAMPLES=ON
make -j$(nproc)

./examples/basic_usage
./examples/session_tracking
./examples/changeset_apply
./examples/changegroup_merge
```

## API Overview

### Database (connection)

```cpp
cloudsqlite::Database db("path/to/db.sqlite");
// or with a custom VFS:
cloudsqlite::Database db("/alias/db", cloudsqlite::OpenFlag::ReadWrite, "bcvfs");

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
cloudsqlite::Session session(db, "main");
session.attach_all();  // Track all tables

// ... application makes changes ...

cloudsqlite::Changeset cs = session.changeset();
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
    [](cloudsqlite::ConflictType type, const cloudsqlite::ChangesetIterator& it) {
        return cloudsqlite::ChangesetAction::Omit;  // Skip conflicts
    });

// Undo via inversion
cloudsqlite::Changeset undo = cs.invert();
undo.apply(db, handler);
```

### Changegroup (merge)

```cpp
cloudsqlite::Changegroup group;
group.add(cs1);
group.add(cs2);
cloudsqlite::Changeset merged = group.output();
merged.apply(target, handler);
```

### bcvfs (cloud-backed storage)

```cpp
// Create and configure the VFS
cloudsqlite::Bcvfs bcvfs("/tmp/cache", "bcvfs");
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
cloudsqlite::Database db("/mydata/mydb.db",
    cloudsqlite::OpenFlag::ReadWrite | cloudsqlite::OpenFlag::Create,
    "bcvfs");

// ... normal SQL operations ...

// Upload changes to cloud
bcvfs.upload("mydata");

// Introspection
bcvfs.register_vtab(db);
auto containers = db.query("SELECT * FROM bcv_container");
```

## Error Handling

All errors are thrown as typed exceptions:

```cpp
try {
    db.execute("INVALID SQL");
} catch (const cloudsqlite::RuntimeError& e) {
    std::cerr << "SQL error: " << e.what() << '\n';
    std::cerr << "Code: " << e.code() << '\n';
} catch (const cloudsqlite::ConstraintError& e) {
    std::cerr << "Constraint violation: " << e.what() << '\n';
} catch (const cloudsqlite::Error& e) {
    std::cerr << "Other error: " << e.what() << '\n';
}
```

## Thread Safety

This library does not add thread safety beyond what SQLite provides. Each `Database` object should be used from a single thread at a time, or with SQLite's serialized threading mode.

## License

MIT
