import { native } from "./native.js";
import { rethrow, rethrowSync } from "./errors.js";
import type { DatabaseOpenOptions, QueryResult, SqlValue } from "./types.js";
import type { NativeDatabase } from "./native.js";

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

  static async open(
    path: string,
    options: DatabaseOpenOptions = {},
  ): Promise<Database> {
    const impl = new native.NativeDatabase();
    await rethrow(() => impl.open(path, options));
    return new Database(impl);
  }

  get isOpen(): boolean {
    return this.#impl.isOpen;
  }

  async execute(sql: string): Promise<void> {
    await this.#serial(() => rethrow(() => this.#impl.execute(sql)));
  }

  async query(sql: string): Promise<QueryResult> {
    const result = await this.#serial(() =>
      rethrow(() => this.#impl.query(sql)),
    );
    return {
      columns: result.columns,
      rows: result.rows as QueryResult["rows"],
    };
  }

  async queryValue(sql: string): Promise<SqlValue> {
    return (await this.#serial(() =>
      rethrow(() => this.#impl.queryValue(sql)),
    )) as SqlValue;
  }

  lastInsertRowid(): number | bigint {
    return this.#impl.lastInsertRowid();
  }

  changes(): number {
    return this.#impl.changes();
  }

  interrupt(): void {
    this.#impl.interrupt();
  }

  setBusyTimeout(ms: number): void {
    this.#impl.setBusyTimeout(ms);
  }

  close(): void {
    rethrowSync(() => this.#impl.close());
  }

  [Symbol.dispose](): void {
    if (this.isOpen) {
      this.close();
    }
  }
}
