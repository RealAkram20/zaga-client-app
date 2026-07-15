# Functionality — Zaga Offline Device Client

What the client does, how it is structured, and where each feature stands. The
wire protocol it implements is specified in the portal repository's
`docs/OFFLINE_CLIENT_GUIDE.md`; this document describes the client side.

## 1. Role in the system

Zaga Device Lock is two applications that share one secret per device.

- The **Online Billing Portal** enrolls a device, takes payment, and *issues* a
  signed 20-character unlock token.
- This **Offline Device Client** gates the Windows login screen, shows the device
  account number, and *verifies* that token locally, with no internet.

The only shared material is the per-device 32-byte `hmac_secret`. The portal
signs; the client verifies. Neither side transmits or logs the secret or a valid
token. If any check cannot complete, the device stays locked — fail closed.

## 2. Why there is no database

The client cannot assume a server or network, so it keeps no SQL database. All
device state lives in one **DPAPI-encrypted file** on the machine
(`C:\ProgramData\Zaga\state.bin`). This is the client's entire persistent store.
The portal's MySQL database is a separate concern in a separate repository.

## 3. Architecture

The security-critical logic is plain C++ with no dependency on the Windows login
screen, so it can be unit-tested on its own. The credential-provider COM layer is
a thin shell on top.

```
src/core/     verification logic (no login-screen dependencies)
  Base32          Crockford Base32 encode/decode + input normalization
  Sha256Hmac      HMAC-SHA256 over Windows CNG (BCrypt)
  TokenCodec      20-char token  <->  (counter, duration, type) + signature
  DeviceState     runtime state: last counter, lock deadline, status
  Verifier        fail-closed accept / replay / deadline state machine
src/store/    encrypted persistence
  DataProtection  machine-scope DPAPI wrapper (CryptProtectData)
  LocalStore      StoredDevice record, versioned serialization, atomic save/load
src/provider/ credential provider COM shell                     [not yet built]
src/service/  enforcement + resilience service                 [not yet built]
```

## 4. The token, in short

A token is 100 bits shown as 20 Crockford-Base32 characters,
`XXXXX-XXXXX-XXXXX-XXXXX`. The first 40 bits are the payload
`(VERSION<<36) | (counter<<16) | (duration_days<<4) | flags`; the last 60 bits are
the top of an HMAC-SHA256 over the payload, keyed by the device secret. The client
recomputes the HMAC and compares it in constant time. See the portal guide for the
normative bit layout and the published test vectors, which `core_tests` checks bit
for bit.

## 5. Local state model

`LocalStore` persists one `StoredDevice` record:

| Field                                   | Purpose                                  |
| --------------------------------------- | ---------------------------------------- |
| `accountNumber`, `serial`, `model`, `name` | Shown on the lock screen              |
| `hmacSecretHex`                         | Verification key (never displayed/logged) |
| `biosPassword`, `recoveryKey`           | Optional recovery credentials            |
| `uninstallCode`                         | Device-generated removal authorization    |
| `state.lastCounter`                     | Highest token counter consumed (anti-replay) |
| `state.lockDeadlineDay`                 | Date the device must lock again           |
| `state.status`                          | active / grace / overdue / locked         |

The whole record is DPAPI-protected at **machine scope**. Machine scope is
required because the credential provider runs as SYSTEM at the logon screen,
before any user has logged on, so user-scope keys are unavailable. Writes go
through a temporary file and an atomic rename, so an interrupted write cannot
leave a corrupt store.

## 6. Verification flow (fail closed)

1. Normalize and Base32-decode the entered token to 100 bits; on any error, reject.
2. Recompute the expected signature and compare in constant time; on mismatch,
   reject (this covers tampering, a wrong secret, and an unknown version).
3. If the token's counter is not greater than `lastCounter`, reject as a replay.
4. Otherwise accept: persist the new counter, extend the lock deadline by
   `duration_days` from whichever is later — today or the current deadline — and
   set the status to active.

Any failure leaves the device locked. The token and the secret are never logged.

## 7. Features and status

| Feature                                                        | Status        |
| -------------------------------------------------------------- | ------------- |
| Token verification core, parity-tested against the portal      | **Done**      |
| Encrypted local state store (DPAPI, machine scope)             | **Done**      |
| Lock screen gating Windows login; show account number + status | Planned (M3)  |
| Enter unlock code and unlock on success                        | Planned (M3)  |
| Self-purchase: type a token bought online on another device    | Planned (M3)  |
| Uninstall authorization (device-generated code)               | Planned (M4)  |
| Enable / disable enforcement (technician-guarded)             | Planned (M4)  |
| Reveal BIOS password / BitLocker recovery key when authorized | Planned (M4)  |
| Self-restarting lock service; registry/file restore; tamper log | Planned (M4) |

## 8. Milestones

1. **Verified token core** — codec, verifier, parity tests. *Complete.*
2. **Encrypted local store** — DPAPI persistence. *Complete.*
3. **Credential provider COM shell** — `ICredentialProvider` /
   `ICredentialProviderCredential`, the actual lock screen, code entry.
4. **Enforcement and resilience** — background service, uninstall-code auth,
   registry/file self-healing, local tamper logging.

## 9. Security rules the client follows

- Never store the shared HMAC secret in plaintext; it lives only inside the
  DPAPI-encrypted store.
- Never log the secret or a valid unlock code.
- Compare signatures in constant time.
- Fail closed everywhere: if a check cannot complete, the device stays locked.
- BIOS passwords and recovery keys are stored encrypted, surfaced only when
  authorized.
