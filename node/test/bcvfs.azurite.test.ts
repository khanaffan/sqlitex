import assert from "node:assert/strict";
import { join } from "node:path";
import { after, before, describe, it } from "node:test";
import {
  BcvContainer,
  Bcvfs,
  Database,
  OpenFlag,
  capabilities,
} from "../src/index.ts";
import {
  AZURITE_ACCOUNT,
  AZURITE_KEY,
  AZURITE_STORAGE,
  isAzuriteRunning,
  rmDir,
  tempDir,
  uniqueContainer,
} from "./helpers.ts";

const skip = !capabilities.hasBcvfs;

describe("Bcvfs + Azurite", { skip }, () => {
  let azurite = false;
  const caches: string[] = [];

  before(async () => {
    azurite = await isAzuriteRunning();
  });

  after(() => {
    for (const dir of caches) {
      rmDir(dir);
    }
  });

  it("creates a container, attaches, opens a database, and uploads", async (t) => {
    if (!azurite) {
      t.skip("Azurite is not running on 127.0.0.1:10000");
      return;
    }

    const container = uniqueContainer("cloud");
    const cacheDir = tempDir("sqlitex-bcvfs-");
    caches.push(cacheDir);

    const creds = {
      storage: AZURITE_STORAGE,
      account: AZURITE_ACCOUNT,
      auth: AZURITE_KEY,
      container,
    };

    await BcvContainer.create(creds);
    try {
      // bcVFS cannot create a database that is absent from the container
      // manifest, so seed one by uploading a local database first.
      const seedDir = tempDir("sqlitex-seed-");
      caches.push(seedDir);
      const seedPath = join(seedDir, "demo.db");
      const seed = await Database.open(seedPath, {
        flags: OpenFlag.ReadWrite | OpenFlag.Create,
      });
      await seed.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT)");
      seed.close();
      await BcvContainer.upload(creds, seedPath, "demo.db");

      let authCalls = 0;
      const logs: string[] = [];
      const bcvfs = new Bcvfs({
        cacheDir,
        vfsName: "bcvfs-node-test",
        cacheSize: 64 * 1024 * 1024,
        maxRequests: 4,
        httpTimeoutSeconds: 15,
        auth: ({ storage, account, container: c }) => {
          authCalls += 1;
          assert.equal(storage, AZURITE_STORAGE);
          assert.equal(account, AZURITE_ACCOUNT);
          assert.equal(c, container);
          return AZURITE_KEY;
        },
        onLog: (message) => {
          logs.push(message);
        },
      });

      assert.equal(bcvfs.vfsName, "bcvfs-node-test");
      assert.equal(bcvfs.isDaemon, false);

      await bcvfs.attach({
        storage: AZURITE_STORAGE,
        account: AZURITE_ACCOUNT,
        container,
        alias: "test",
      });
      assert.ok(authCalls >= 1);

      const changed = await bcvfs.poll("test");
      assert.equal(typeof changed, "boolean");

      const db = await Database.open("/test/demo.db", {
        flags: OpenFlag.ReadWrite,
        bcvfs,
      });
      await db.execute("INSERT INTO t(name) VALUES ('cloud')");
      assert.equal(await db.queryValue("SELECT name FROM t"), "cloud");
      db.close();

      const uploaded = await bcvfs.upload("test");
      assert.ok(uploaded >= 0);
      assert.ok(logs.length > 0, "expected bcvfs log callback to fire");

      // Round-trip through cloud storage to prove the upload really landed.
      const verifyPath = join(seedDir, "verify.db");
      await BcvContainer.download(creds, "demo.db", verifyPath);
      const verify = await Database.open(verifyPath, {
        flags: OpenFlag.ReadOnly,
      });
      assert.equal(await verify.queryValue("SELECT name FROM t"), "cloud");
      verify.close();

      await bcvfs.detach("test");

      await BcvContainer.delete(creds, "demo.db");
      await assert.rejects(() =>
        BcvContainer.download(creds, "demo.db", join(seedDir, "gone.db")),
      );
    } finally {
      await BcvContainer.destroy(creds).catch(() => undefined);
    }
  });

  it("surfaces an exception thrown by the auth callback", async (t) => {
    if (!azurite) {
      t.skip("Azurite is not running on 127.0.0.1:10000");
      return;
    }

    const container = uniqueContainer("autherr");
    const cacheDir = tempDir("sqlitex-bcvfs-");
    caches.push(cacheDir);
    const creds = {
      storage: AZURITE_STORAGE,
      account: AZURITE_ACCOUNT,
      auth: AZURITE_KEY,
      container,
    };

    await BcvContainer.create(creds);
    try {
      const bcvfs = new Bcvfs({
        cacheDir,
        vfsName: "bcvfs-node-autherr",
        auth: () => {
          throw new Error("token fetch exploded");
        },
      });
      await assert.rejects(
        () =>
          bcvfs.attach({
            storage: AZURITE_STORAGE,
            account: AZURITE_ACCOUNT,
            container,
            alias: "test",
          }),
        /token fetch exploded/,
      );
    } finally {
      await BcvContainer.destroy(creds).catch(() => undefined);
    }
  });
});
