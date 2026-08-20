import assert from "node:assert/strict";
import { after, describe, it } from "node:test";
import {
  Database,
  OpenFlag,
  SqlitexError,
  capabilities,
} from "../src/index.ts";

describe("Database", () => {
  const dbs: Database[] = [];
  after(() => {
    for (const db of dbs) {
      try {
        db.close();
      } catch {
        // already closed
      }
    }
  });

  async function memory(): Promise<Database> {
    const db = await Database.open(":memory:", {
      flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
    });
    dbs.push(db);
    return db;
  }

  it("opens an in-memory database", async () => {
    const db = await memory();
    assert.equal(db.isOpen, true);
  });

  it("executes SQL and queries rows", async () => {
    const db = await memory();
    await db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
    await db.execute("INSERT INTO t(name) VALUES ('alice')");
    assert.equal(db.changes(), 1);
    const result = await db.query("SELECT id, name FROM t");
    assert.deepEqual(result.columns, ["id", "name"]);
    assert.equal(result.rows.length, 1);
    assert.equal(result.rows[0]?.name, "alice");
  });

  it("returns queryValue for a scalar", async () => {
    const db = await memory();
    await db.execute("CREATE TABLE t (n INTEGER)");
    await db.execute("INSERT INTO t VALUES (42)");
    assert.equal(await db.queryValue("SELECT n FROM t"), 42);
    assert.equal(await db.queryValue("SELECT n FROM t WHERE n = 0"), null);
  });

  it("wraps SQL errors", async () => {
    const db = await memory();
    await assert.rejects(
      () => db.execute("NOT SQL"),
      (err: unknown) => {
        assert.ok(err instanceof SqlitexError);
        assert.ok((err as SqlitexError).code !== 0);
        return true;
      },
    );
  });

  it("exposes capability flags", () => {
    assert.equal(typeof capabilities.hasBcvfs, "boolean");
    assert.equal(typeof capabilities.hasSession, "boolean");
  });
});
