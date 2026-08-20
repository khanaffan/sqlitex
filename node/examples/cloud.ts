import { BcvContainer, Bcvfs, Database, OpenFlag } from "../src/index.ts";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const storage = "azure?emulator=127.0.0.1:10000";
const account = "devstoreaccount1";
const auth =
  "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";
const container = `sqlitex-example-${Date.now()}`;

const creds = { storage, account, auth, container };
await BcvContainer.create(creds);

// bcVFS will not create a database that is missing from the container
// manifest, so seed one by uploading a local database first.
const seedDir = mkdtempSync(join(tmpdir(), "sqlitex-seed-"));
const seedPath = join(seedDir, "app.db");
const seed = await Database.open(seedPath, {
  flags: OpenFlag.ReadWrite | OpenFlag.Create,
});
await seed.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, note TEXT)");
seed.close();
await BcvContainer.upload(creds, seedPath, "app.db");

const cacheDir = mkdtempSync(join(tmpdir(), "sqlitex-example-"));
const bcvfs = new Bcvfs({
  cacheDir,
  vfsName: "bcvfs-example",
  auth: () => auth,
});

await bcvfs.attach({ storage, account, container, alias: "demo" });

const db = await Database.open("/demo/app.db", {
  flags: OpenFlag.ReadWrite,
  bcvfs,
});
await db.execute("INSERT INTO t(note) VALUES ('hello cloud')");
db.close();

console.log("uploaded", await bcvfs.upload("demo"));
await BcvContainer.destroy(creds);
