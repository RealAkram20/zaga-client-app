# Zaga Offline Device Client

Native Windows lock client for the Zaga Device Lock platform. It gates the Windows
login screen on a financed device, shows the device account number, and verifies
unlock tokens **locally, with no internet**. If any check cannot complete, the
device stays locked (fail closed).

This is the companion to the **Online Billing Portal** (separate repository). The
portal signs unlock tokens; this client verifies them. The only shared material is
the per-device 32-byte `hmac_secret`.

The protocol is specified in the portal repository's `docs/OFFLINE_CLIENT_GUIDE.md`
and the coding rules in `docs/DEVELOPMENT_STANDARDS.md`.

- [docs/BUILD.md](docs/BUILD.md) — prerequisites, build and test commands, troubleshooting.
- [docs/FUNCTIONALITY.md](docs/FUNCTIONALITY.md) — architecture, state model, and feature status.

## Architecture

The credential-provider COM layer is kept separate from the verification logic so
the security-critical code is unit-testable outside the Windows login screen.

```
src/core/     plain C++, no login-screen dependencies (testable)
  Base32      Crockford Base32 encode/decode + input normalization
  Sha256Hmac  HMAC-SHA256 over Windows CNG (BCrypt)
  TokenCodec  20-char token <-> (counter, duration, type) + signature check
  Verifier    fail-closed accept / replay / deadline state machine
src/store/    encrypted local state (DPAPI)            [milestone 2]
src/provider/ ICredentialProvider COM shell             [milestone 3]
tests/        parity tests against the portal's published vectors
```

## Build

Requires Visual Studio 2022 with the C++ (ATL) workload and the Windows 10/11 SDK.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## Status

- [x] Milestone 1 — verified token core with parity tests
- [ ] Milestone 2 — DPAPI encrypted local store
- [ ] Milestone 3 — credential provider COM shell
- [ ] Milestone 4 — enforcement service, uninstall auth, resilience
