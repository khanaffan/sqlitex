# Contributing to sqlitex

Thank you for your interest in contributing to `sqlitex`!

## Prerequisites

- **C++23** compiler (Clang 17+ / AppleClang 16+, GCC 13+, or MSVC 2022 17.8+)
- **CMake** 3.21+
- **libcurl** and **OpenSSL** (optional; required only when `SQLITEX_BCVFS=ON`)
- **clang-format** (recommended; CI enforces formatting)

## Building and Testing

Use the included `CMakePresets.json` for standard workflows:

```bash
# Full Debug build (Session + bcVFS + tests + examples)
cmake --preset default
cmake --build --preset default --parallel
ctest --preset default

# Core-only build (no external dependencies)
cmake --preset minimal
cmake --build --preset minimal --parallel
ctest --preset minimal

# AddressSanitizer + UndefinedBehaviorSanitizer (Linux/macOS)
cmake --preset asan
cmake --build --preset asan --parallel
ctest --preset asan
```

## Code Style and Design Guidelines

- **Formatting:** Run `cmake --build <build-dir> --target clang-format` before
  committing. CI verifies formatting with `clang-format-check`.
- **Clean public headers:** Do not `#include <sqlite3.h>` in any file under
  `include/sqlitex/`. Place opaque forward declarations and constants in
  `include/sqlitex/detail/sqlite3_fwd.h` or `include/sqlitex/detail/session_api.h`.
- **DLL-safe exports:** Export public classes and free functions with
  `SQLITEX_EXPORT` (from `sqlitex_export.h`) and avoid calling `sqlite3_*`
  directly from inline header code so Windows shared-library builds link cleanly.
- **Compiler warnings:** Library code under `include/` and `src/` must compile
  cleanly under `-Wall -Wextra -Wpedantic` (GCC/Clang) and `/W4` (MSVC).
- **Tests:** Add focused Google Test cases in `tests/` for every bug fix or new
  API surface. Prefer offline deterministic tests over external services.

## Pull Requests

1. Fork the repository and create a branch from `main`.
2. Make focused, minimal changes with accompanying tests and documentation
   updates.
3. Verify formatting and tests pass locally (including `asan` when modifying
   memory management, callbacks, or C interop).
4. Open a pull request describing the motivation, changes, and verification steps.
