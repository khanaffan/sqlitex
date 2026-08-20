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

  static fromBuffer(buffer: Buffer): Changeset {
    requireSessionFeature("Changeset");
    const Ctor = native.NativeChangeset!;
    return new Changeset(rethrowSync(() => Ctor.fromBuffer(buffer)));
  }

  get size(): number {
    return this.#impl.size;
  }

  get empty(): boolean {
    return this.#impl.empty;
  }

  toBuffer(): Buffer {
    return this.#impl.toBuffer();
  }

  concat(other: Changeset): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.concat(other.#impl)));
  }

  invert(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.invert()));
  }

  async apply(db: Database, options: ApplyOptions = {}): Promise<void> {
    await rethrow(() => this.#impl.apply(db.native, options));
  }
}

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

  get size(): number {
    return this.#impl.size;
  }

  get empty(): boolean {
    return this.#impl.empty;
  }

  toBuffer(): Buffer {
    return this.#impl.toBuffer();
  }

  async apply(db: Database, options: ApplyOptions = {}): Promise<void> {
    await rethrow(() => this.#impl.apply(db.native, options));
  }
}

export class Changegroup {
  readonly #impl: NativeChangegroup;

  constructor() {
    requireSessionFeature("Changegroup");
    this.#impl = new native.NativeChangegroup!();
  }

  add(item: Changeset | Patchset): void {
    rethrowSync(() => this.#impl.add(item.native));
  }

  output(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.output()));
  }
}
