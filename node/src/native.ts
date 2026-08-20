import { createRequire } from "node:module";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import type {
  ApplyOptions,
  BcvContainerOptions,
  BcvfsOptions,
} from "./types.js";

export interface NativeCapabilities {
  readonly hasBcvfs: boolean;
  readonly hasSession: boolean;
}

export interface NativeQueryResult {
  columns: string[];
  rows: Array<Record<string, unknown>>;
}

export interface NativeDatabase {
  open(
    path: string,
    options?: { flags?: number; vfs?: string; bcvfs?: { vfsName: string } },
  ): Promise<void>;
  close(): void;
  execute(sql: string): Promise<void>;
  query(sql: string): Promise<NativeQueryResult>;
  queryValue(sql: string): Promise<unknown>;
  lastInsertRowid(): number | bigint;
  changes(): number;
  interrupt(): void;
  setBusyTimeout(ms: number): void;
  readonly isOpen: boolean;
}

export interface NativeSession {
  enable(enabled?: boolean): boolean;
  setIndirect(indirect?: boolean): boolean;
  attach(table?: string): void;
  attachAll(): void;
  setTableFilter(filter?: ((table: string) => boolean) | null): void;
  changeset(): NativeChangeset;
  patchset(): NativePatchset;
  diff(table: string, fromDb?: string): NativeChangeset;
  readonly isEmpty: boolean;
  readonly memoryUsed: number;
  close(): void;
}

export interface NativeChangeset {
  readonly size: number;
  readonly empty: boolean;
  toBuffer(): Buffer;
  concat(other: NativeChangeset): NativeChangeset;
  invert(): NativeChangeset;
  apply(db: NativeDatabase, options?: ApplyOptions): Promise<void>;
}

export interface NativePatchset {
  readonly size: number;
  readonly empty: boolean;
  toBuffer(): Buffer;
  apply(db: NativeDatabase, options?: ApplyOptions): Promise<void>;
}

export interface NativeChangegroup {
  add(cs: NativeChangeset | NativePatchset): void;
  output(): NativeChangeset;
}

export interface NativeBcvfs {
  readonly vfsName: string;
  readonly isDaemon: boolean;
  setAuthCallback(
    cb:
      | string
      | ((info: {
          storage: string;
          account: string;
          container: string;
        }) => string | false)
      | null,
  ): void;
  setLogCallback(cb: ((message: string) => void) | null): void;
  attach(opts: {
    storage: string;
    account: string;
    container: string;
    alias: string;
  }): Promise<void>;
  detach(alias: string): Promise<void>;
  upload(
    container: string,
    options?: { onBusy?: (retry: number) => number | null },
  ): Promise<number>;
  poll(container: string): Promise<boolean>;
  copy(opts: { container: string; from: string; to: string }): Promise<void>;
  remove(opts: { container: string; dbName: string }): Promise<void>;
  revert(container: string): Promise<void>;
  registerVtab(db: NativeDatabase): void;
}

export interface NativeAddon {
  capabilities: NativeCapabilities;
  NativeDatabase: new () => NativeDatabase;
  NativeSession?: new (db: NativeDatabase, dbName?: string) => NativeSession;
  NativeChangeset?: (new () => NativeChangeset) & {
    fromBuffer(buf: Buffer): NativeChangeset;
  };
  NativePatchset?: new () => NativePatchset;
  NativeChangegroup?: new () => NativeChangegroup;
  NativeBcvfs?: new (opts: BcvfsOptions) => NativeBcvfs;
  NativeBcvContainer?: {
    create(opts: BcvContainerOptions): Promise<void>;
    destroy(opts: BcvContainerOptions): Promise<void>;
    upload(
      opts: BcvContainerOptions,
      localPath: string,
      remoteName: string,
    ): Promise<void>;
    download(
      opts: BcvContainerOptions,
      remoteName: string,
      localPath: string,
    ): Promise<void>;
    delete(opts: BcvContainerOptions, remoteName: string): Promise<void>;
  };
}

function packageRoot(): string {
  const start = dirname(fileURLToPath(import.meta.url));
  let dir = start;
  for (let i = 0; i < 8; i++) {
    if (
      existsSync(join(dir, "package.json")) &&
      existsSync(join(dir, "CMakeLists.txt"))
    ) {
      return dir;
    }
    const parent = dirname(dir);
    if (parent === dir) {
      break;
    }
    dir = parent;
  }
  return join(start, "..");
}

function loadAddon(): NativeAddon {
  const root = packageRoot();
  const candidates = [
    join(root, "build", "Release", "sqlitex_node.node"),
    join(root, "build", "Debug", "sqlitex_node.node"),
  ];
  const require = createRequire(import.meta.url);
  for (const file of candidates) {
    if (existsSync(file)) {
      return require(file) as NativeAddon;
    }
  }
  throw new Error(
    `sqlitex native addon not found. Run \`npm run build:native\` in ${root}. Tried:\n${candidates.join("\n")}`,
  );
}

export const native: NativeAddon = loadAddon();
export const capabilities: NativeCapabilities = native.capabilities;
