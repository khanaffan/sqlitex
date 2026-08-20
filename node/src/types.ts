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
  readonly indirect: boolean;
  readonly primaryKey: readonly boolean[];
  readonly oldValues: readonly SqlValue[];
  readonly newValues: readonly SqlValue[];
}

/** {@link ChangeInfo} plus conflict-specific fields. */
export interface ConflictInfo extends ChangeInfo {
  readonly type: ConflictType;
  readonly conflictValues: readonly SqlValue[];
  readonly fkConflicts: number;
}

/** Result of {@link Database.query}. */
export interface QueryResult {
  readonly columns: readonly string[];
  readonly rows: ReadonlyArray<Readonly<Record<string, SqlValue>>>;
}

/** Options for {@link Database.open}. */
export interface DatabaseOpenOptions {
  readonly flags?: number;
  readonly vfs?: string;
  readonly bcvfs?: { readonly vfsName: string };
}

/** Construction options for {@link Bcvfs}. */
export interface BcvfsOptions {
  readonly cacheDir: string;
  readonly vfsName?: string;
  readonly cacheSize?: number;
  readonly maxRequests?: number;
  readonly httpTimeoutSeconds?: number;
  readonly httpLogging?: boolean;
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
  readonly storage: string;
  readonly account: string;
  readonly container: string;
  readonly alias: string;
}

/** Cloud credentials and layout for {@link BcvContainer} operations. */
export interface BcvContainerOptions {
  readonly storage: string;
  readonly account: string;
  readonly auth: string;
  readonly container: string;
  readonly nameSize?: number;
  readonly blockSize?: number;
}

/** Options for {@link Changeset.apply} / {@link Patchset.apply}. */
export interface ApplyOptions {
  readonly onConflict?: (info: ConflictInfo) => ChangesetAction;
  readonly tableFilter?: (table: string) => boolean;
  readonly changeFilter?: (info: ChangeInfo) => boolean;
  readonly flags?: number;
}

/** Compile-time features of the loaded native addon. See {@link capabilities}. */
export interface Capabilities {
  readonly hasBcvfs: boolean;
  readonly hasSession: boolean;
}
