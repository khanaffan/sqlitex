/** SQLite `sqlite3_open_v2` flags. Combine with bitwise OR. */
export const OpenFlag = {
  ReadOnly: 0x00000001,
  ReadWrite: 0x00000002,
  Create: 0x00000004,
  Uri: 0x00000040,
  Memory: 0x00000080,
  NoMutex: 0x00008000,
  FullMutex: 0x00010000,
  SharedCache: 0x00020000,
  PrivateCache: 0x00040000,
} as const;

export type OpenFlagName = keyof typeof OpenFlag;

/** Flags for {@link ApplyOptions.flags} (`sqlite3changeset_apply_v2`). */
export const ApplyFlag = {
  None: 0,
  NoSavepoint: 0x0001,
  Invert: 0x0002,
  IgnoreNoop: 0x0004,
  FkNoAction: 0x0008,
} as const;

/** Value returned from queries and changeset iterators. */
export type SqlValue = number | bigint | string | Buffer | null;

/** Kind of row change in a changeset. */
export type ChangeOp = "insert" | "delete" | "update";

/**
 * Why {@link ApplyOptions.onConflict} was invoked.
 *
 * - `data` — destination row differs from the changeset old values
 * - `notFound` — expected row is missing
 * - `conflict` — INSERT into an existing PRIMARY KEY
 * - `constraint` — CHECK / UNIQUE / NOT NULL
 * - `foreignKey` — FK violations after the apply (`fkConflicts`)
 */
export type ConflictType =
  "data" | "notFound" | "conflict" | "constraint" | "foreignKey";

/**
 * How to resolve a conflict:
 * `omit` keeps the local row, `replace` takes the changeset, `abort`
 * rolls the apply back.
 */
export type ChangesetAction = "omit" | "replace" | "abort";

/** One change record, as passed to conflict/change filters. */
export interface ChangeInfo {
  readonly op: ChangeOp;
  readonly table: string;
  readonly columnCount: number;
  /** True when the change was made by a trigger or FK action. */
  readonly indirect: boolean;
  readonly primaryKey: readonly boolean[];
  /** Pre-image; empty for INSERT. */
  readonly oldValues: readonly SqlValue[];
  /** Post-image; empty for DELETE. */
  readonly newValues: readonly SqlValue[];
}

/** {@link ChangeInfo} plus conflict-specific fields. */
export interface ConflictInfo extends ChangeInfo {
  readonly type: ConflictType;
  /** Conflicting destination values, when available. */
  readonly conflictValues: readonly SqlValue[];
  /** Count of FK violations (`foreignKey` conflicts only). */
  readonly fkConflicts: number;
}

/** Result of {@link Database.query}. */
export interface QueryResult {
  readonly columns: readonly string[];
  readonly rows: ReadonlyArray<Readonly<Record<string, SqlValue>>>;
}

/** Options for {@link Database.open}. */
export interface DatabaseOpenOptions {
  /** Bitmask of {@link OpenFlag} values. */
  readonly flags?: number;
  /** SQLite VFS name. Ignored when `bcvfs` is set. */
  readonly vfs?: string;
  /** Open through this CloudSQLite VFS (`/alias/db` paths). */
  readonly bcvfs?: { readonly vfsName: string };
}

/** Construction options for {@link Bcvfs}. */
export interface BcvfsOptions {
  /** Local directory for the block cache. */
  readonly cacheDir: string;
  /** SQLite VFS name to register. Default is assigned by the addon. */
  readonly vfsName?: string;
  /** Cache size in bytes. */
  readonly cacheSize?: number;
  /** Max concurrent HTTP requests. */
  readonly maxRequests?: number;
  readonly httpTimeoutSeconds?: number;
  readonly httpLogging?: boolean;
  /** Static token or synchronous auth callback. */
  readonly auth?: string | BcvfsAuthCallback;
  readonly onLog?: (message: string) => void;
}

/** Identity of the container an auth callback is asked to authorize. */
export interface BcvfsAuthInfo {
  readonly storage: string;
  readonly account: string;
  readonly container: string;
}

/**
 * Return an auth token, or `false` to deny. Must be synchronous; a Promise
 * is rejected.
 */
export type BcvfsAuthCallback = (info: BcvfsAuthInfo) => string | false;

/** Arguments to {@link Bcvfs.attach}. */
export interface BcvfsAttachOptions {
  /** Provider URI, e.g. `azure` or `azure?emulator=127.0.0.1:10000`. */
  readonly storage: string;
  readonly account: string;
  readonly container: string;
  /** Local alias used in paths as `/alias/dbName`. */
  readonly alias: string;
}

/** Cloud credentials and layout for {@link BcvContainer} operations. */
export interface BcvContainerOptions {
  readonly storage: string;
  readonly account: string;
  /** Account key or SAS token. */
  readonly auth: string;
  readonly container: string;
  readonly nameSize?: number;
  readonly blockSize?: number;
}

/** Options for {@link Changeset.apply} / {@link Patchset.apply}. */
export interface ApplyOptions {
  /** Must be synchronous. Return `omit` | `replace` | `abort`. */
  readonly onConflict?: (info: ConflictInfo) => ChangesetAction;
  /** Skip tables for which this returns false. Synchronous. */
  readonly tableFilter?: (table: string) => boolean;
  /** Skip individual changes for which this returns false. Synchronous. */
  readonly changeFilter?: (info: ChangeInfo) => boolean;
  /** Bitmask of {@link ApplyFlag} values. */
  readonly flags?: number;
}

/** Compile-time features of the loaded native addon. See {@link capabilities}. */
export interface Capabilities {
  readonly hasBcvfs: boolean;
  readonly hasSession: boolean;
}
