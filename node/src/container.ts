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

/**
 * Manage CloudSQLite containers and the database files inside them
 * (create/destroy, upload/download/delete) without a live {@link Bcvfs}.
 *
 * Requires `SQLITEX_BCVFS=ON`.
 */
export class BcvContainer {
  /** Create an empty container in cloud storage. */
  static async create(options: BcvContainerOptions): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.create(options));
  }

  /** Destroy the container and its contents. */
  static async destroy(options: BcvContainerOptions): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.destroy(options));
  }

  /**
   * Upload a local SQLite database file into the container so bcVFS can
   * open it by `remoteName`.
   */
  static async upload(
    options: BcvContainerOptions,
    localPath: string,
    remoteName: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.upload(options, localPath, remoteName));
  }

  /** Download a database from the container to `localPath`. */
  static async download(
    options: BcvContainerOptions,
    remoteName: string,
    localPath: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.download(options, remoteName, localPath));
  }

  /** Delete a database from the container by remote name. */
  static async delete(
    options: BcvContainerOptions,
    remoteName: string,
  ): Promise<void> {
    const api = requireContainer();
    await rethrow(() => api.delete(options, remoteName));
  }
}
