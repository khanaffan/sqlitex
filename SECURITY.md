# Security Policy

## Supported Versions

| Version | Supported |
| ------- | --------- |
| `0.1.x` / `main` | Yes |

## Reporting a Vulnerability

Please **do not** report security vulnerabilities through public GitHub issues.

Instead, report them privately using GitHub Security Advisories:
<https://github.com/khanaffan/sqlitex/security/advisories/new>

Include as much of the following as possible:
- A clear description of the vulnerability and potential impact
- Minimal reproduction steps or proof-of-concept code
- Compiler, platform, and CMake options (`SQLITEX_SESSION`, `SQLITEX_BCVFS`,
  `BUILD_SHARED_LIBS`)

You can expect an acknowledgment within 5 business days and coordinated
disclosure once a fix is verified.

## Upstream Dependencies

`sqlitex` bundles SQLite 3.54.1 (`third_party/sqlite3/`) and CloudSQLite
(`third_party/bcvfs/`). Vulnerabilities in upstream SQLite should also be
reported to the SQLite team according to <https://sqlite.org/cves.html>.
