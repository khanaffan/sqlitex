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

export const ApplyFlag = {
  None: 0,
  NoSavepoint: 0x0001,
  Invert: 0x0002,
  IgnoreNoop: 0x0004,
  FkNoAction: 0x0008,
} as const;

export type SqlValue = number | bigint | string | Buffer | null;

export type ChangeOp = "insert" | "delete" | "update";

export type ConflictType =
  "data" | "notFound" | "conflict" | "constraint" | "foreignKey";

export type ChangesetAction = "omit" | "replace" | "abort";

export interface ChangeInfo {
  readonly op: ChangeOp;
  readonly table: string;
  readonly columnCount: number;
  readonly indirect: boolean;
  readonly primaryKey: readonly boolean[];
  readonly oldValues: readonly SqlValue[];
  readonly newValues: readonly SqlValue[];
}

export interface ConflictInfo extends ChangeInfo {
  readonly type: ConflictType;
  readonly conflictValues: readonly SqlValue[];
  readonly fkConflicts: number;
}

export interface QueryResult {
  readonly columns: readonly string[];
  readonly rows: ReadonlyArray<Readonly<Record<string, SqlValue>>>;
}

export interface DatabaseOpenOptions {
  readonly flags?: number;
  readonly vfs?: string;
  readonly bcvfs?: { readonly vfsName: string };
}

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

export interface BcvfsAuthInfo {
  readonly storage: string;
  readonly account: string;
  readonly container: string;
}

export type BcvfsAuthCallback = (info: BcvfsAuthInfo) => string | false;

export interface BcvfsAttachOptions {
  readonly storage: string;
  readonly account: string;
  readonly container: string;
  readonly alias: string;
}

export interface BcvContainerOptions {
  readonly storage: string;
  readonly account: string;
  readonly auth: string;
  readonly container: string;
  readonly nameSize?: number;
  readonly blockSize?: number;
}

export interface ApplyOptions {
  readonly onConflict?: (info: ConflictInfo) => ChangesetAction;
  readonly tableFilter?: (table: string) => boolean;
  readonly changeFilter?: (info: ChangeInfo) => boolean;
  readonly flags?: number;
}

export interface Capabilities {
  readonly hasBcvfs: boolean;
  readonly hasSession: boolean;
}
