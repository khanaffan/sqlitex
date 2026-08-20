import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  Changegroup,
  Changeset,
  Database,
  OpenFlag,
  Session,
  capabilities,
} from "../src/index.ts";

const skip = !capabilities.hasSession;

describe("Session / Changeset / Changegroup", { skip }, () => {
  async function seeded(): Promise<Database> {
    const db = await Database.open(":memory:", {
      flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
    });
    await db.execute(`
      CREATE TABLE items (
        id INTEGER PRIMARY KEY,
        name TEXT NOT NULL,
        quantity INTEGER DEFAULT 0
      )
    `);
    await db.execute("INSERT INTO items VALUES (1, 'Widget', 10)");
    await db.execute("INSERT INTO items VALUES (2, 'Gadget', 5)");
    return db;
  }

  it("tracks inserts and extracts a changeset", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    session.attachAll();
    assert.equal(session.isEmpty, true);

    await db.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");
    assert.equal(session.isEmpty, false);

    const cs = session.changeset();
    assert.equal(cs.empty, false);
    assert.ok(cs.size > 0);

    const buf = cs.toBuffer();
    assert.ok(Buffer.isBuffer(buf));
    assert.equal(buf.length, cs.size);

    const copy = Changeset.fromBuffer(buf);
    assert.equal(copy.size, cs.size);
    assert.notEqual(copy.toBuffer().buffer, buf.buffer);

    session.close();
    db.close();
  });

  it("applies a changeset to another database", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");
    const cs = session.changeset();

    const dst = await seeded();
    await cs.apply(dst);
    assert.equal(await dst.queryValue("SELECT COUNT(*) FROM items"), 3);
    assert.equal(
      await dst.queryValue("SELECT name FROM items WHERE id = 3"),
      "Doodad",
    );

    session.close();
    src.close();
    dst.close();
  });

  it("inverts a changeset", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    session.attachAll();
    await db.execute("INSERT INTO items VALUES (3, 'Temp', 1)");
    const cs = session.changeset();
    await cs.invert().apply(db);
    assert.equal(await db.queryValue("SELECT COUNT(*) FROM items"), 2);
    session.close();
    db.close();
  });

  it("merges changesets with Changegroup", async () => {
    const db = await seeded();
    const s1 = new Session(db);
    s1.enable(true);
    s1.attachAll();
    await db.execute("INSERT INTO items VALUES (3, 'A', 1)");
    const cs1 = s1.changeset();
    s1.close();

    const s2 = new Session(db);
    s2.enable(true);
    s2.attachAll();
    await db.execute("INSERT INTO items VALUES (4, 'B', 2)");
    const cs2 = s2.changeset();
    s2.close();

    const group = new Changegroup();
    group.add(cs1);
    group.add(cs2);
    const merged = group.output();
    assert.equal(merged.empty, false);

    const dst = await seeded();
    await merged.apply(dst);
    assert.equal(await dst.queryValue("SELECT COUNT(*) FROM items"), 4);
    db.close();
    dst.close();
  });

  it("invokes a conflict handler", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("UPDATE items SET quantity = 99 WHERE id = 1");
    const cs = session.changeset();

    const dst = await seeded();
    await dst.execute("UPDATE items SET quantity = 50 WHERE id = 1");

    const seen: string[] = [];
    await cs.apply(dst, {
      onConflict: (info) => {
        seen.push(info.type);
        assert.equal(info.table, "items");
        return "omit";
      },
    });
    assert.deepEqual(seen, ["data"]);
    // "omit" must leave the local row untouched.
    assert.equal(
      await dst.queryValue("SELECT quantity FROM items WHERE id = 1"),
      50,
    );
    session.close();
    src.close();
    dst.close();
  });

  it("honours a replace conflict action", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("UPDATE items SET quantity = 99 WHERE id = 1");
    const cs = session.changeset();

    const dst = await seeded();
    await dst.execute("UPDATE items SET quantity = 50 WHERE id = 1");
    await cs.apply(dst, { onConflict: () => "replace" });
    assert.equal(
      await dst.queryValue("SELECT quantity FROM items WHERE id = 1"),
      99,
    );
    session.close();
    src.close();
    dst.close();
  });

  it("aborts the apply when the handler returns abort", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("UPDATE items SET quantity = 99 WHERE id = 1");
    await src.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");
    const cs = session.changeset();

    const dst = await seeded();
    await dst.execute("UPDATE items SET quantity = 50 WHERE id = 1");
    await assert.rejects(() => cs.apply(dst, { onConflict: () => "abort" }));
    // Abort rolls the whole changeset back.
    assert.equal(await dst.queryValue("SELECT COUNT(*) FROM items"), 2);
    session.close();
    src.close();
    dst.close();
  });

  it("rejects when a conflict handler throws", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("UPDATE items SET quantity = 99 WHERE id = 1");
    const cs = session.changeset();

    const dst = await seeded();
    await dst.execute("UPDATE items SET quantity = 50 WHERE id = 1");
    await assert.rejects(
      () =>
        cs.apply(dst, {
          onConflict: () => {
            throw new Error("boom from handler");
          },
        }),
      /boom from handler/,
    );
    session.close();
    src.close();
    dst.close();
  });

  it("rejects an asynchronous conflict handler", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("UPDATE items SET quantity = 99 WHERE id = 1");
    const cs = session.changeset();

    const dst = await seeded();
    await dst.execute("UPDATE items SET quantity = 50 WHERE id = 1");
    await assert.rejects(
      () =>
        cs.apply(dst, {
          onConflict: () => Promise.resolve("omit") as never,
        }),
      /synchronous/,
    );
    session.close();
    src.close();
    dst.close();
  });

  it("reports foreign key conflicts without misusing the iterator", async () => {
    const src = await Database.open(":memory:", {
      flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
    });
    await src.execute("CREATE TABLE parent (id INTEGER PRIMARY KEY)");
    await src.execute(
      "CREATE TABLE child (id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id))",
    );
    await src.execute("INSERT INTO parent VALUES (1)");
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("INSERT INTO child VALUES (10, 1)");
    const cs = session.changeset();

    // Destination has no matching parent row, so applying with FK enforcement
    // on produces a FOREIGN_KEY conflict, for which the iterator exposes only
    // the conflict count.
    const dst = await Database.open(":memory:", {
      flags: OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory,
    });
    await dst.execute("CREATE TABLE parent (id INTEGER PRIMARY KEY)");
    await dst.execute(
      "CREATE TABLE child (id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id))",
    );
    await dst.execute("PRAGMA foreign_keys = ON");

    const seen: { type: string; fk?: number }[] = [];
    await cs.apply(dst, {
      onConflict: (info) => {
        seen.push({ type: info.type, fk: info.fkConflicts });
        return "omit";
      },
    });
    const fk = seen.find((s) => s.type === "foreignKey");
    assert.ok(
      fk,
      `expected a foreignKey conflict, got ${JSON.stringify(seen)}`,
    );
    assert.ok((fk.fk ?? 0) >= 1);
    session.close();
    src.close();
    dst.close();
  });

  it("rejects a non-Database argument to apply", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    session.attachAll();
    await db.execute("INSERT INTO items VALUES (3, 'Doodad', 25)");
    const cs = session.changeset();
    // Passing the wrong wrapped native object must not be reinterpreted.
    await assert.rejects(
      async () =>
        await (cs as unknown as { apply(x: unknown): Promise<unknown> }).apply(
          session,
        ),
      /Database/,
    );
    session.close();
    db.close();
  });

  it("surfaces an exception thrown by a table filter", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    session.setTableFilter(() => {
      throw new Error("filter exploded");
    });
    await db.execute("UPDATE items SET quantity = 1 WHERE id = 1");
    assert.throws(() => session.isEmpty, /filter exploded/);
    session.close();
    db.close();
  });

  it("filters tables", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    const asked: string[] = [];
    session.setTableFilter((table) => {
      asked.push(table);
      return table !== "items";
    });
    await db.execute("UPDATE items SET quantity = 1 WHERE id = 1");
    assert.ok(asked.includes("items"));
    assert.equal(session.isEmpty, true);

    await db.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT)");
    await db.execute("INSERT INTO notes(body) VALUES ('kept')");
    assert.ok(asked.includes("notes"));
    assert.equal(session.isEmpty, false);
    session.close();
    db.close();
  });

  it("rejects closing a database while a session is alive", async () => {
    const db = await seeded();
    const session = new Session(db);
    assert.throws(() => db.close(), /dependents/);
    session.close();
    db.close();
  });

  it("extracts and applies a patchset", async () => {
    const src = await seeded();
    const session = new Session(src);
    session.enable(true);
    session.attachAll();
    await src.execute("INSERT INTO items VALUES (3, 'Patch', 7)");
    const ps = session.patchset();
    assert.equal(ps.empty, false);
    assert.ok(ps.size > 0);
    assert.ok(Buffer.isBuffer(ps.toBuffer()));

    const dst = await seeded();
    await ps.apply(dst);
    assert.equal(await dst.queryValue("SELECT COUNT(*) FROM items"), 3);
    assert.equal(
      await dst.queryValue("SELECT name FROM items WHERE id = 3"),
      "Patch",
    );

    session.close();
    src.close();
    dst.close();
  });

  it("toBuffer can be called repeatedly", async () => {
    const db = await seeded();
    const session = new Session(db);
    session.enable(true);
    session.attachAll();
    await db.execute("INSERT INTO items VALUES (3, 'X', 1)");
    const cs = session.changeset();
    const a = cs.toBuffer();
    const b = cs.toBuffer();
    assert.deepEqual(a, b);
    session.close();
    db.close();
  });
});
