# sqlitex for Node.js

TypeScript-first N-API bindings for the C++ `sqlitex` library. Milestone 1
covers a minimal `Database`, CloudSQLite (`Bcvfs` + `BcvContainer`), and
session/change-tracking (`Session`, `Changeset`, `Patchset`, `Changegroup`).

This package lives **in-repo** and is **source-build only** for v0. There are
no prebuilt binaries and **Electron is not supported**.

## Requirements

- Node.js 18.18+
- CMake 3.15+
- A C++17 compiler
- curl + OpenSSL (for CloudSQLite / `SQLITEX_BCVFS=ON`, the default)

## Build

From this directory:

```bash
npm install          # compiles the native addon via cmake-js
npm run build:ts     # emit ESM + typings into dist/
npm test             # unit tests; Azurite tests skip if the emulator is down
npm run lint         # ESLint
npm run format:check # Prettier
```

macOS automatically passes Homebrew `openssl@3` to CMake when present.
Windows uses `VCPKG_INSTALLATION_ROOT` when set.

Disable optional features:

```bash
SQLITEX_BCVFS=OFF npm run build:native
SQLITEX_SESSION=OFF npm run build:native
```

`capabilities.hasBcvfs` / `capabilities.hasSession` report what the addon
was compiled with. Constructing a disabled API throws instead of silently
omitting the export.

## Usage

### Session / changeset

```ts
import { Database, OpenFlag, Session } from "sqlitex";

const db = await Database.open(":memory:", {
  flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
});
await db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");

const session = new Session(db);
session.enable(true);
session.attachAll();
await db.execute("INSERT INTO t(name) VALUES ('alice')");

const cs = session.changeset();
const replica = await Database.open(":memory:", {
  flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
});
await replica.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
await cs.apply(replica);
```

### CloudSQLite + Azurite

Start the emulator yourself (tests do **not** spawn it):

```bash
npm install -g azurite
azurite --silent --location /tmp/azurite
# or: npm run azurite
```

```ts
import { BcvContainer, Bcvfs, Database, OpenFlag } from "sqlitex";

const storage = "azure?emulator=127.0.0.1:10000";
const account = "devstoreaccount1";
const auth = "<azurite well-known key>";

const creds = { storage, account, auth, container: "demo" };
await BcvContainer.create(creds);

// bcVFS only opens databases already present in the container manifest.
// Seed one by uploading a local database file.
await BcvContainer.upload(creds, "/tmp/seed.db", "app.db");

const bcvfs = new Bcvfs({
  cacheDir: "/tmp/sqlite-cache",
  auth: () => auth, // must be synchronous
});
await bcvfs.attach({ storage, account, container: "demo", alias: "c" });

const db = await Database.open("/c/app.db", {
  flags: OpenFlag.ReadWrite,
  bcvfs,
});
```

Auth, busy, conflict, and table-filter callbacks are **synchronous**. Returning
a Promise is rejected. Token acquisition that needs I/O must happen before the
native call.

An exception thrown inside a callback is never swallowed: a conflict handler
rejects the `apply()` promise, an auth callback rejects the enclosing bcVFS
operation, and a table filter surfaces on the next `Session` call.

`BcvContainer` also exposes `upload`, `download`, and `delete` for managing the
databases inside a container.

### Concurrency

Calls on a single `Database` are serialized; a synchronous accessor
(`lastInsertRowid`, `changes`, `setBusyTimeout`) or any `Session` method throws
if async work on the same connection is still in flight. `Database` and
`Session` implement `Symbol.dispose`, so they work with `using`.

Loading the addon into a `worker_thread` is supported — each thread gets its
own class constructors — but a single `Database` must not be shared across
threads.

## API

Compile-time features: `capabilities.hasBcvfs` / `capabilities.hasSession`.
Constructing a disabled API throws.

| Export                   | Role                                                                                                |
| ------------------------ | --------------------------------------------------------------------------------------------------- |
| `Database`               | Connection: `open`, `execute`, `query`, `queryValue`, `close`                                       |
| `Session`                | Record changes; extract `changeset()` / `patchset()`                                                |
| `Changeset`              | `fromBuffer`, `toBuffer`, `invert`, `concat`, `apply`                                               |
| `Patchset`               | Compact changeset (no UPDATE old values)                                                            |
| `Changegroup`            | Merge changesets/patchsets, then `output()`                                                         |
| `Bcvfs`                  | CloudSQLite VFS: `attach`, `upload`, `poll`, `detach`                                               |
| `BcvContainer`           | Create/destroy containers; upload/download/delete DBs                                               |
| `OpenFlag` / `ApplyFlag` | Bit flags for open and changeset apply                                                              |
| `SqlitexError`           | Base error; subclasses: `RuntimeError`, `ConstraintError`, `BusyError`, `IoError`, `ChangesetError` |

### Database

```ts
const db = await Database.open(path, { flags?, vfs?, bcvfs? });
await db.execute(sql);
const { columns, rows } = await db.query(sql);
const value = await db.queryValue(sql); // first column of first row, or null
db.lastInsertRowid();
db.changes();
db.setBusyTimeout(ms);
db.interrupt();
db.close(); // throws if a Session is still open
```

### Session / changeset

```ts
const session = new Session(db); // optional schema name, default "main"
session.enable(true);
session.attachAll(); // or attach("table")
session.setTableFilter((table) => table !== "skip"); // sync only
const cs = session.changeset();
const ps = session.patchset();
session.close();

await cs.apply(db, {
  onConflict: (info) => "omit", // "replace" | "abort"
  flags: ApplyFlag.None,
});
Changeset.fromBuffer(cs.toBuffer());
cs.invert();
const group = new Changegroup();
group.add(cs);
group.output();
```

### CloudSQLite

```ts
await BcvContainer.create(creds);
await BcvContainer.upload(creds, localPath, "app.db");

const bcvfs = new Bcvfs({ cacheDir, auth: () => token });
await bcvfs.attach({ storage, account, container, alias: "c" });
const db = await Database.open("/c/app.db", {
  flags: OpenFlag.ReadWrite,
  bcvfs,
});
await bcvfs.upload("c");
await bcvfs.poll("c");
await bcvfs.detach("c");
```

JSDoc on the TypeScript sources is emitted into `dist/*.d.ts`.

## Packaging

v0 is source-build only (`cmake-js compile` on `npm install`). A prebuild
matrix (`prebuildify` / `node-gyp-build`) is deferred.

N-API version is pinned to **8**. The published surface is **ESM**
(`dist/index.js` + `dist/index.d.ts`). CommonJS callers can use
`await import("sqlitex")`.
