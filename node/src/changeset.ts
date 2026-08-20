import { native } from "./native.js";
import { rethrow, rethrowSync, SqlitexError } from "./errors.js";
import type { Database } from "./database.js";
import type { ApplyOptions } from "./types.js";
import type {
  NativeChangeset,
  NativeChangegroup,
  NativePatchset,
} from "./native.js";

function requireSessionFeature(name: string): void {
  if (!native.capabilities.hasSession) {
    throw new SqlitexError(
      `${name} requires session support (SQLITEX_SESSION=ON)`,
      { kind: "misuse" },
    );
  }
}

/**
 * A SQLite session changeset: insert/update/delete records that can be
 * stored, inverted, concatenated, or {@link apply | applied} to another
 * database.
 *
 * Requires `SQLITEX_SESSION=ON`.
 */
export class Changeset {
  readonly #impl: NativeChangeset;

  /** @internal */
  constructor(impl: NativeChangeset) {
    this.#impl = impl;
  }

  /** @internal */
  get native(): NativeChangeset {
    return this.#impl;
  }

  /** Parse a changeset from bytes previously produced by {@link toBuffer}. */
  static fromBuffer(buffer: Buffer): Changeset {
    requireSessionFeature("Changeset");
    const Ctor = native.NativeChangeset!;
    return new Changeset(rethrowSync(() => Ctor.fromBuffer(buffer)));
  }

  /** Serialized size in bytes. */
  get size(): number {
    return this.#impl.size;
  }

  /** True when the changeset contains no changes. */
  get empty(): boolean {
    return this.#impl.empty;
  }

  /** Copy of the serialized changeset. Safe to call repeatedly. */
  toBuffer(): Buffer {
    return this.#impl.toBuffer();
  }

  /** Concatenate `other` after this changeset. */
  concat(other: Changeset): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.concat(other.#impl)));
  }

  /** Return a changeset that undoes these changes when applied. */
  invert(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.invert()));
  }

  /**
   * Apply this changeset to `db`.
   *
   * Conflict, table-filter, and change-filter callbacks must be
   * synchronous. A thrown handler error rejects the promise. `"abort"`
   * rolls the whole apply back.
   */
  async apply(db: Database, options: ApplyOptions = {}): Promise<void> {
    await rethrow(() => this.#impl.apply(db.native, options));
  }
}

/**
 * Compact form of a changeset. UPDATE records omit old values, so conflict
 * detection is weaker than {@link Changeset}.
 */
export class Patchset {
  readonly #impl: NativePatchset;

  /** @internal */
  constructor(impl: NativePatchset) {
    this.#impl = impl;
  }

  /** @internal */
  get native(): NativePatchset {
    return this.#impl;
  }

  /** Serialized size in bytes. */
  get size(): number {
    return this.#impl.size;
  }

  /** True when the patchset contains no changes. */
  get empty(): boolean {
    return this.#impl.empty;
  }

  /** Copy of the serialized patchset. */
  toBuffer(): Buffer {
    return this.#impl.toBuffer();
  }

  /** Apply this patchset to `db`. See {@link Changeset.apply}. */
  async apply(db: Database, options: ApplyOptions = {}): Promise<void> {
    await rethrow(() => this.#impl.apply(db.native, options));
  }
}

/**
 * Combines multiple changesets or patchsets into one changeset, resolving
 * overlapping changes on the same rows.
 */
export class Changegroup {
  readonly #impl: NativeChangegroup;

  constructor() {
    requireSessionFeature("Changegroup");
    this.#impl = new native.NativeChangegroup!();
  }

  /** Fold `item` into this group. */
  add(item: Changeset | Patchset): void {
    rethrowSync(() => this.#impl.add(item.native));
  }

  /** Combined changeset of every item added so far. */
  output(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.output()));
  }
}
