import { Changegroup, Database, OpenFlag, Session } from "../src/index.ts";

const flags = OpenFlag.ReadWrite | OpenFlag.Create | OpenFlag.Memory;

const src = await Database.open(":memory:", { flags });
await src.execute(
  "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL)",
);
await src.execute("INSERT INTO items VALUES (1, 'seed')");

const session = new Session(src);
session.enable(true);
session.attachAll();
await src.execute("INSERT INTO items VALUES (2, 'tracked')");

const changeset = session.changeset();
console.log("changeset bytes", changeset.size);

const dst = await Database.open(":memory:", { flags });
await dst.execute(
  "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL)",
);
await dst.execute("INSERT INTO items VALUES (1, 'seed')");
await changeset.apply(dst);

const group = new Changegroup();
group.add(changeset);
console.log("merged", group.output().size);
console.log(await dst.query("SELECT * FROM items"));

session.close();
src.close();
dst.close();
