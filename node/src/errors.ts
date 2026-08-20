export type SqlitexErrorKind =
  | "error"
  | "runtime"
  | "constraint"
  | "busy"
  | "io"
  | "corrupt"
  | "cantOpen"
  | "auth"
  | "misuse"
  | "range"
  | "notFound"
  | "notADb"
  | "schema"
  | "full"
  | "abort"
  | "permission"
  | "protocol"
  | "internal"
  | "mismatch"
  | "changeset"
  | "changesetApply";

export class SqlitexError extends Error {
  readonly code: number;
  readonly extendedCode: number;
  readonly kind: SqlitexErrorKind;

  constructor(
    message: string,
    options: {
      code?: number;
      extendedCode?: number;
      kind?: SqlitexErrorKind;
      cause?: unknown;
    } = {},
  ) {
    super(
      message,
      options.cause !== undefined ? { cause: options.cause } : undefined,
    );
    this.name = "SqlitexError";
    this.code = options.code ?? -1;
    this.extendedCode = options.extendedCode ?? 0;
    this.kind = options.kind ?? "error";
  }
}

export class RuntimeError extends SqlitexError {
  constructor(
    message: string,
    options: ConstructorParameters<typeof SqlitexError>[1] = {},
  ) {
    super(message, { ...options, kind: options.kind ?? "runtime" });
    this.name = "RuntimeError";
  }
}

export class ConstraintError extends SqlitexError {
  constructor(
    message: string,
    options: ConstructorParameters<typeof SqlitexError>[1] = {},
  ) {
    super(message, { ...options, kind: "constraint" });
    this.name = "ConstraintError";
  }
}

export class BusyError extends SqlitexError {
  constructor(
    message: string,
    options: ConstructorParameters<typeof SqlitexError>[1] = {},
  ) {
    super(message, { ...options, kind: "busy" });
    this.name = "BusyError";
  }
}

export class IoError extends SqlitexError {
  constructor(
    message: string,
    options: ConstructorParameters<typeof SqlitexError>[1] = {},
  ) {
    super(message, { ...options, kind: "io" });
    this.name = "IoError";
  }
}

export class ChangesetError extends SqlitexError {
  constructor(
    message: string,
    options: ConstructorParameters<typeof SqlitexError>[1] = {},
  ) {
    super(message, { ...options, kind: options.kind ?? "changeset" });
    this.name = "ChangesetError";
  }
}

interface NativeLikeError {
  message?: string;
  code?: number;
  extendedCode?: number;
  kind?: string;
  name?: string;
}

function isNativeLike(err: unknown): err is NativeLikeError {
  return typeof err === "object" && err !== null && "message" in err;
}

export function wrapNativeError(err: unknown): SqlitexError {
  if (err instanceof SqlitexError) {
    return err;
  }
  if (!isNativeLike(err)) {
    return new SqlitexError(String(err));
  }
  const kind = (err.kind ?? "error") as SqlitexErrorKind;
  const opts = {
    code: err.code,
    extendedCode: err.extendedCode,
    kind,
    cause: err,
  };
  switch (kind) {
    case "constraint":
      return new ConstraintError(err.message ?? "constraint error", opts);
    case "busy":
      return new BusyError(err.message ?? "busy", opts);
    case "io":
      return new IoError(err.message ?? "io error", opts);
    case "runtime":
      return new RuntimeError(err.message ?? "runtime error", opts);
    case "changeset":
    case "changesetApply":
      return new ChangesetError(err.message ?? "changeset error", opts);
    default:
      return new SqlitexError(err.message ?? "sqlitex error", opts);
  }
}

export async function rethrow<T>(fn: () => Promise<T>): Promise<T> {
  try {
    return await fn();
  } catch (err) {
    throw wrapNativeError(err);
  }
}

export function rethrowSync<T>(fn: () => T): T {
  try {
    return fn();
  } catch (err) {
    throw wrapNativeError(err);
  }
}
