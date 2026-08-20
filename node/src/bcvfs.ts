import { native } from "./native.js";
import { rethrow, rethrowSync, SqlitexError } from "./errors.js";
import type { Database } from "./database.js";
import type {
  BcvfsAttachOptions,
  BcvfsAuthCallback,
  BcvfsOptions,
} from "./types.js";
import type { NativeBcvfs } from "./native.js";

function requireBcvfs(): NonNullable<typeof native.NativeBcvfs> {
  if (!native.NativeBcvfs) {
    throw new SqlitexError(
      "sqlitex was built without CloudSQLite support (SQLITEX_BCVFS=OFF)",
      { kind: "misuse" },
    );
  }
  return native.NativeBcvfs;
}

/**
 * CloudSQLite block-cache VFS. Attach a container alias, then
 * {@link Database.open} paths like `/alias/name.db`.
 *
 * Requires `SQLITEX_BCVFS=ON`. Auth and busy callbacks must be synchronous.
 */
export class Bcvfs {
  readonly #impl: NativeBcvfs;

  constructor(options: BcvfsOptions) {
    const Ctor = requireBcvfs();
    this.#impl = rethrowSync(() => new Ctor(options));
    // Register callbacks after the ObjectWrap exists. Creating a TSFN inside
    // the native constructor leaves the function unusable (attach never
    // reaches JS).
    if (typeof options.auth === "string") {
      const token = options.auth;
      this.#impl.setAuthCallback(() => token);
    } else if (options.auth) {
      this.#impl.setAuthCallback(options.auth);
    }
    if (options.onLog) {
      this.#impl.setLogCallback(options.onLog);
    }
  }

  /** @internal */
  get native(): NativeBcvfs {
    return this.#impl;
  }

  /** Registered SQLite VFS name, from {@link BcvfsOptions.vfsName}. */
  get vfsName(): string {
    return this.#impl.vfsName;
  }

  /** True when this instance talks to a `blockcachevfsd` daemon. */
  get isDaemon(): boolean {
    return this.#impl.isDaemon;
  }

  /**
   * Replace the auth callback. Must return a token string or `false`
   * synchronously. Pass `null` to clear.
   */
  setAuthCallback(callback: BcvfsAuthCallback | null): void {
    rethrowSync(() => this.#impl.setAuthCallback(callback));
  }

  /** Receive VFS log lines. Pass `null` to clear. */
  setLogCallback(callback: ((message: string) => void) | null): void {
    this.#impl.setLogCallback(callback);
  }

  /**
   * Attach a cloud container under `alias`. Databases are then opened as
   * `/alias/<dbName>`.
   */
  async attach(options: BcvfsAttachOptions): Promise<void> {
    await rethrow(() => this.#impl.attach(options));
  }

  /** Detach a previously attached alias. */
  async detach(alias: string): Promise<void> {
    await rethrow(() => this.#impl.detach(alias));
  }

  /**
   * Upload local cache dirty blocks for `container` (the attach alias).
   *
   * @param onBusy - Synchronous retry delay in milliseconds, or `null` to
   *   stop. Called when the upload lock is held.
   * @returns Number of blocks uploaded.
   */
  async upload(
    container: string,
    options?: { onBusy?: (retry: number) => number | null },
  ): Promise<number> {
    return rethrow(() => this.#impl.upload(container, options));
  }

  /**
   * Fetch remote manifest updates for `container`.
   * @returns Whether the local cache changed.
   */
  async poll(container: string): Promise<boolean> {
    return rethrow(() => this.#impl.poll(container));
  }

  /** Copy a database inside an attached container. */
  async copy(options: {
    container: string;
    from: string;
    to: string;
  }): Promise<void> {
    await rethrow(() => this.#impl.copy(options));
  }

  /** Delete a database from an attached container. */
  async remove(options: { container: string; dbName: string }): Promise<void> {
    await rethrow(() => this.#impl.remove(options));
  }

  /** Discard un-uploaded local changes for `container`. */
  async revert(container: string): Promise<void> {
    await rethrow(() => this.#impl.revert(container));
  }

  /** Register `bcv_container` / related virtual tables on `db`. */
  registerVtab(db: Database): void {
    rethrowSync(() => this.#impl.registerVtab(db.native));
  }
}
