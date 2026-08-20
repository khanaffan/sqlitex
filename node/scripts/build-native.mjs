#!/usr/bin/env node
import { spawnSync, execSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(dirname(fileURLToPath(import.meta.url)));
const cmakeJs = join(root, "node_modules", "cmake-js", "bin", "cmake-js");

const args = ["compile", "--directory", root, "--out", join(root, "build")];

if (process.env.SQLITEX_BCVFS === "0" || process.env.SQLITEX_BCVFS === "OFF") {
  args.push("--CDSQLITEX_BCVFS=OFF");
}
if (
  process.env.SQLITEX_SESSION === "0" ||
  process.env.SQLITEX_SESSION === "OFF"
) {
  args.push("--CDSQLITEX_SESSION=OFF");
}

if (process.platform === "darwin") {
  try {
    const prefix = execSync("brew --prefix openssl@3", {
      encoding: "utf8",
    }).trim();
    if (prefix && existsSync(prefix)) {
      args.push(`--CDOPENSSL_ROOT_DIR=${prefix}`);
    }
  } catch {
    // OpenSSL may already be discoverable.
  }
}

if (process.platform === "win32" && process.env.VCPKG_INSTALLATION_ROOT) {
  args.push(
    `--CDCMAKE_TOOLCHAIN_FILE=${process.env.VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake`,
  );
}

if (process.env.CMAKE_BUILD_TYPE) {
  args.push("--config", process.env.CMAKE_BUILD_TYPE);
}

const extra = process.argv.slice(2);
const result = spawnSync(process.execPath, [cmakeJs, ...args, ...extra], {
  stdio: "inherit",
  cwd: root,
  env: process.env,
});

process.exit(result.status ?? 1);
