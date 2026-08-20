/**
 * TypeScript bindings for sqlitex: a minimal `Database`, CloudSQLite
 * (`Bcvfs` / `BcvContainer`), and session/changeset APIs.
 *
 * Feature availability is reported by {@link capabilities}. Constructing a
 * disabled API throws instead of omitting the export.
 *
 * @packageDocumentation
 */
export { capabilities } from "./native.js";
export { Database } from "./database.js";
export { Session } from "./session.js";
export { Changeset, Patchset, Changegroup } from "./changeset.js";
export { Bcvfs } from "./bcvfs.js";
export { BcvContainer } from "./container.js";
export {
  SqlitexError,
  RuntimeError,
  ConstraintError,
  BusyError,
  IoError,
  ChangesetError,
} from "./errors.js";
export { OpenFlag, ApplyFlag } from "./types.js";
export type {
  SqlValue,
  QueryResult,
  DatabaseOpenOptions,
  BcvfsOptions,
  BcvfsAuthCallback,
  BcvfsAuthInfo,
  BcvfsAttachOptions,
  BcvContainerOptions,
  ApplyOptions,
  ChangeInfo,
  ConflictInfo,
  ChangeOp,
  ConflictType,
  ChangesetAction,
  Capabilities,
} from "./types.js";
