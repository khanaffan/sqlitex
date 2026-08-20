import { native } from "./native.js";
import { rethrow, rethrowSync } from "./errors.js";
import type { DatabaseOpenOptions, QueryResult, SqlValue } from "./types.js";
import type { NativeDatabase } from "./native.js";

/**
 * SQLite connection. Async SQL calls on one instance are serialized so they
 * do not occupy extra libuv threadpool threads while waiting on the lock.
 *
 * Synchronous accessors (`lastInsertRowid`, `changes`, `setBusyTimeout`)
 * and any `Session` method throw if async work on this connection is still
 * in flight. Implements `Symbol.dispose`.
 */
export class Database {
  readonly #impl: NativeDatabase;
  // Native async work occupies a libuv threadpool thread while it waits for
  // the connection lock, so calls are serialized here instead of piling up
  // and starving unrelated fs/dns/crypto work.
  #queue: Promise<unknown> = Promise.resolve();

  #serial<T>(fn: () => Promise<T>): Promise<T> {
    const run = this.#queue.then(fn, fn);
    this.#queue = run.then(
      () => undefined,
      () => undefined,
    );
    return run;
  }

  private constructor(impl: NativeDatabase) {
    this.#impl = impl;
  }

  /** @internal */
  get native(): NativeDatabase {
    return this.#impl;
  }

  /**
   * Open a database file, URI, or `:memory:` database.
   *
   * Pass {@link DatabaseOpenOptions.bcvfs} (or `vfs`) to open through a
   * CloudSQLite VFS. The remote database must already exist in the container
   * manifest.
   */
  static async open(
    path: string,
    options: DatabaseOpenOptions = {},
  ): Promise<Database> {
    const impl = new native.NativeDatabase();
    await rethrow(() => impl.open(path, options));
    return new Database(impl);
  }

  /** Whether the underlying connection is still open. */
  get isOpen(): boolean {
    return this.#impl.isOpen;
  }

  /** Execute one or more SQL statements, ignoring result rows. */
  async execute(sql: string): Promise<void> {
    await this.#serial(() => rethrow(() => this.#impl.execute(sql)));
  }

  /** Run a query and return column names plus all rows. */
  async query(sql: string): Promise<QueryResult> {
    const result = await this.#serial(() =>
      rethrow(() => this.#impl.query(sql)),
    );
    return {
      columns: result.columns,
      rows: result.rows as QueryResult["rows"],
    };
  }

  /** Return the first column of the first row, or `null` if empty. */
  async queryValue(sql: string): Promise<SqlValue> {
    return (await this.#serial(() =>
      rethrow(() => this.#impl.queryValue(sql)),
    )) as SqlValue;
  }

  /** Rowid of the most recent successful insert. */
  lastInsertRowid(): number | bigint {
    return this.#impl.lastInsertRowid();
  }

  /** Number of rows modified by the last insert/update/delete. */
  changes(): number {
    return this.#impl.changes();
  }

  /** Abort a running statement on this connection. */
  interrupt(): void {
    this.#impl.interrupt();
  }

  /** Sleep this many milliseconds when SQLite returns SQLITE_BUSY. */
  setBusyTimeout(ms: number): void {
    this.#impl.setBusyTimeout(ms);
  }

  /** Close the connection. Throws if a `Session` still depends on it. */
  close(): void {
    rethrowSync(() => this.#impl.close());
  }

  [Symbol.dispose](): void {
    if (this.isOpen) {
      this.close();
    }
  }
}
