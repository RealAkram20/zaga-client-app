# Build Guide — Zaga Offline Device Client

How to build, test, and troubleshoot the native Windows client. Target platform
is **Windows 10/11 x64**.

## 1. Prerequisites

- **Visual Studio 2022** (Community is fine) with:
  - *Desktop development with C++* workload
  - *C++ ATL for latest v143 build tools (x86 & x64)* component
  - *Windows 10/11 SDK* (10.0.19041 or newer; the credential-provider milestone
    uses the SDK verified in this repo, 10.0.26100)
- **CMake 3.20+** — the copy bundled with Visual Studio works:
  `…\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
- **Git**

No third-party libraries are used. Cryptography comes from Windows itself
(CNG `bcrypt` for HMAC-SHA256, DPAPI `crypt32` for the encrypted store).

## 2. Configure and build

From the repository root:

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

`-A x64` is required; the client is 64-bit only. Use `--config Release` for a
release build.

If `cmake` is not on your PATH, either open a *Developer Command Prompt for
VS 2022* (which adds it) or call the bundled copy by full path.

## 3. Run the tests

```
ctest --test-dir build -C Debug --output-on-failure
```

Or run the test executables directly:

```
build\Debug\core_tests.exe
build\Debug\store_tests.exe
```

- `core_tests` proves the token codec matches the portal's published vectors bit
  for bit, and that the verifier rejects tampered tokens, wrong secrets, and
  replayed counters.
- `store_tests` exercises the DPAPI-encrypted local store end to end.

Per the development standards, a change to code generation or verification is not
merged without these passing.

## 4. Build targets and layout

| Target        | Kind          | Contents                                   |
| ------------- | ------------- | ------------------------------------------ |
| `zaga_core`   | static lib    | `src/core` (codec, verifier) + `src/store` |
| `core_tests`  | executable    | token codec / verifier parity tests        |
| `store_tests` | executable    | DPAPI store round-trip and tamper tests     |

Build output lives under `build/` and is not committed (see `.gitignore`).

## 5. Compiler settings that matter

The top-level `CMakeLists.txt` sets these deliberately:

- `/W4 /permissive-` — high warnings and standards-conformant mode.
- `_WIN32_WINNT=0x0A00`, `WINVER=0x0A00` — target Windows 10 so the SDK exposes
  the DPAPI and credential-provider APIs. Without a target version the SDK
  defaults can hide newer declarations.
- `WIN32_LEAN_AND_MEAN`, `NOMINMAX` — trim the Windows headers and stop the
  `min`/`max` macros from clashing with `std::max`.

## 6. Troubleshooting

**`CryptProtectData` / `CRYPTPROTECT_LOCAL_MACHINE` undeclared.**
Do not name any header on the include path in a way that collides with a Windows
SDK header. The filesystem is case-insensitive, so a project header `Dpapi.h`
under an `-I` directory satisfies `#include <dpapi.h>` and shadows the real SDK
header, hiding the DPAPI declarations. This is why the DPAPI wrapper class lives
in `DataProtection.h`, not `Dpapi.h`. The same caution applies to any future
header (`Credential.h`, `Registry.h`, and so on) — check for a same-named SDK
header first.

**Wrong architecture / link errors about x86 vs x64.**
Always configure with `-A x64`. Delete `build/` and reconfigure if you switched.

**CMake picks the wrong SDK.**
Pass `-DCMAKE_SYSTEM_VERSION=10.0.26100.0` (or your installed version) at
configure time to pin the Windows SDK.
