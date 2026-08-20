import { native } from "./native.js";
import { rethrow, SqlitexError } from "./errors.js";
import type { BcvContainerOptions } from "./types.js";

function requireContainer(): NonNullable<typeof native.NativeBcvContainer> {
  if (!native.NativeBcvContainer) {
    throw new SqlitexError(
      "sqlitex was built without CloudSQLite support (SQLITEX_BCVFS=OFF)",
      { kind: "misuse" },
    );
  }
  return native.NativeBcvContainer;
}

export class BcvContainer {
  static async create(options: BcvContainerOptions): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.create(options));
  }

  static async destroy(options: BcvContainerOptions): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.destroy(options));
  }

  /** Uploads a local SQLite database file into the container. */
  static async upload(
    options: BcvContainerOptions,
    localPath: string,
    remoteName: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.upload(options, localPath, remoteName));
  }

  /** Downloads a database from the container to a local file. */
  static async download(
    options: BcvContainerOptions,
    remoteName: string,
    localPath: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.download(options, remoteName, localPath));
  }

  /** Deletes a database from the container. */
  static async delete(
    options: BcvContainerOptions,
    remoteName: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.delete(options, remoteName));
  }
}
