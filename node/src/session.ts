import { native } from "./native.js";
import { rethrowSync, SqlitexError } from "./errors.js";
import { Changeset, Patchset } from "./changeset.js";
import type { Database } from "./database.js";
import type { NativeSession } from "./native.js";

function requireSession(): NonNullable<typeof native.NativeSession> {
  if (!native.NativeSession) {
    throw new SqlitexError(
      "sqlitex was built without session support (SQLITEX_SESSION=OFF)",
      { kind: "misuse" },
    );
  }
  return native.NativeSession;
}

/**
 * Records changes on a {@link Database} for later extract as a
 * {@link Changeset} or {@link Patchset}.
 *
 * Requires a build with `SQLITEX_SESSION=ON` (`capabilities.hasSession`).
 * Implements `Symbol.dispose`. The database cannot be closed while a
 * session is still open.
 */
export class Session {
  readonly #impl: NativeSession;

  /**
   * @param db - Open database to track.
   * @param dbName - Attached schema name, default `"main"`.
   */
  constructor(db: Database, dbName = "main") {
    const Ctor = requireSession();
    this.#impl = rethrowSync(() => new Ctor(db.native, dbName));
  }

  /** Enable or disable recording. Returns the previous enabled state. */
  enable(enabled = true): boolean {
    return rethrowSync(() => this.#impl.enable(enabled));
  }

  /**
   * Treat subsequent changes as indirect (for example, from triggers).
   * Returns the previous indirect flag.
   */
  setIndirect(indirect = true): boolean {
    return rethrowSync(() => this.#impl.setIndirect(indirect));
  }

  /**
   * Start tracking `table`. Omit `table` to attach every table (same as
   * {@link attachAll}).
   */
  attach(table?: string): void {
    rethrowSync(() => this.#impl.attach(table));
  }

  /** Track every table in the schema. */
  attachAll(): void {
    rethrowSync(() => this.#impl.attachAll());
  }

  /**
   * Decide which tables to track. The callback must be synchronous;
   * returning a Promise is rejected. Thrown errors surface on the next
   * Session call (for example {@link isEmpty} or {@link changeset}).
   *
   * Pass `null` to clear the filter.
   */
  setTableFilter(filter?: ((table: string) => boolean) | null): void {
    rethrowSync(() => this.#impl.setTableFilter(filter ?? null));
  }

  /** Extract a full changeset of recorded changes. */
  changeset(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.changeset()));
  }

  /**
   * Extract a patchset (smaller than a changeset; old values for UPDATE
   * are omitted).
   */
  patchset(): Patchset {
    return new Patchset(rethrowSync(() => this.#impl.patchset()));
  }

  /**
   * Record the difference between `table` in `fromDb` and the same table
   * on this session's database.
   */
  diff(table: string, fromDb = "main"): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.diff(table, fromDb)));
  }

  /** True when no changes have been recorded. */
  get isEmpty(): boolean {
    return this.#impl.isEmpty;
  }

  /** Bytes of memory used by the session changeset buffer. */
  get memoryUsed(): number {
    return this.#impl.memoryUsed;
  }

  /** Detach from the database so it can be closed. */
  close(): void {
    this.#impl.close();
  }

  [Symbol.dispose](): void {
    this.close();
  }
}
