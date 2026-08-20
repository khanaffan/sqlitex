import net from "node:net";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

export const AZURITE_HOST = "127.0.0.1";
export const AZURITE_PORT = 10000;
export const AZURITE_ACCOUNT = "devstoreaccount1";
export const AZURITE_KEY =
  "Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw==";
export const AZURITE_STORAGE = "azure?emulator=127.0.0.1:10000";

export function tempDir(prefix: string): string {
  return mkdtempSync(join(tmpdir(), prefix));
}

export function rmDir(path: string): void {
  rmSync(path, { recursive: true, force: true });
}

export function isAzuriteRunning(
  host = AZURITE_HOST,
  port = AZURITE_PORT,
): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = net.connect({ host, port });
    const done = (up: boolean) => {
      socket.removeAllListeners();
      socket.destroy();
      resolve(up);
    };
    socket.setTimeout(1000);
    socket.once("connect", () => done(true));
    socket.once("timeout", () => done(false));
    socket.once("error", () => done(false));
  });
}

export function uniqueContainer(testName: string): string {
  const clean = testName
    .toLowerCase()
    .replace(/[^a-z0-9]/g, "")
    .slice(0, 20);
  return `sqlitexn-${clean}-${Date.now()}-${Math.floor(Math.random() * 1e4)}`;
}
