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

  get vfsName(): string {
    return this.#impl.vfsName;
  }

  get isDaemon(): boolean {
    return this.#impl.isDaemon;
  }

  setAuthCallback(callback: BcvfsAuthCallback | null): void {
    rethrowSync(() => this.#impl.setAuthCallback(callback));
  }

  setLogCallback(callback: ((message: string) => void) | null): void {
    this.#impl.setLogCallback(callback);
  }

  async attach(options: BcvfsAttachOptions): Promise<void> {
    await rethrow(() => this.#impl.attach(options));
  }

  async detach(alias: string): Promise<void> {
    await rethrow(() => this.#impl.detach(alias));
  }

  async upload(
    container: string,
    options?: { onBusy?: (retry: number) => number | null },
  ): Promise<number> {
    return rethrow(() => this.#impl.upload(container, options));
  }

  async poll(container: string): Promise<boolean> {
    return rethrow(() => this.#impl.poll(container));
  }

  async copy(options: {
    container: string;
    from: string;
    to: string;
  }): Promise<void> {
    await rethrow(() => this.#impl.copy(options));
  }

  async remove(options: { container: string; dbName: string }): Promise<void> {
    await rethrow(() => this.#impl.remove(options));
  }

  async revert(container: string): Promise<void> {
    await rethrow(() => this.#impl.revert(container));
  }

  registerVtab(db: Database): void {
    rethrowSync(() => this.#impl.registerVtab(db.native));
  }
}
