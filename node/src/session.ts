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

export class Session {
  readonly #impl: NativeSession;

  constructor(db: Database, dbName = "main") {
    const Ctor = requireSession();
    this.#impl = rethrowSync(() => new Ctor(db.native, dbName));
  }

  enable(enabled = true): boolean {
    return rethrowSync(() => this.#impl.enable(enabled));
  }

  setIndirect(indirect = true): boolean {
    return rethrowSync(() => this.#impl.setIndirect(indirect));
  }

  attach(table?: string): void {
    rethrowSync(() => this.#impl.attach(table));
  }

  attachAll(): void {
    rethrowSync(() => this.#impl.attachAll());
  }

  setTableFilter(filter?: ((table: string) => boolean) | null): void {
    rethrowSync(() => this.#impl.setTableFilter(filter ?? null));
  }

  changeset(): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.changeset()));
  }

  patchset(): Patchset {
    return new Patchset(rethrowSync(() => this.#impl.patchset()));
  }

  diff(table: string, fromDb = "main"): Changeset {
    return new Changeset(rethrowSync(() => this.#impl.diff(table, fromDb)));
  }

  get isEmpty(): boolean {
    return this.#impl.isEmpty;
  }

  get memoryUsed(): number {
    return this.#impl.memoryUsed;
  }

  close(): void {
    this.#impl.close();
  }

  [Symbol.dispose](): void {
    this.close();
  }
}
