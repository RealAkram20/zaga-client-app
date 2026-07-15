# Connectivity — Device ↔ Portal API

The offline client verifies unlock tokens locally and never depends on the network
to do so. The portal API is additive: it handles provisioning, check-in, and token
convenience. If the network is unavailable, the device still verifies typed codes
offline and stays fail-closed.

Base URL is configurable per install (the portal's address, e.g.
`http://localhost/zagatech` in development). All calls are JSON.

## Authentication

Each device holds a per-device bearer token (Laravel Sanctum), obtained once at
enrollment and stored in the client's encrypted state. Revoking the token in the
portal cuts a single device off without touching any other.

## Endpoints

### POST /api/device/enroll

Redeems a one-time enrollment code and provisions the device. No auth. Throttled.

Request:

```json
{ "code": "XO3UTYBP43", "agent_version": "0.1.0" }
```

Response 200:

```json
{
  "token": "1|abcd…",
  "account_number": "ZG-40000",
  "hmac_secret": "abab…64 hex…",
  "serial": "5CG9482Q1B",
  "model": "Dell Latitude 5490",
  "name": "Reception PC"
}
```

Invalid or expired code returns 422. The code is consumed on first success. The
client writes the account number and secret into its encrypted store and keeps the
token for later calls.

### POST /api/device/heartbeat

Auth: `Authorization: Bearer <token>`. Reports the device is alive and records its
last-seen time and agent version. Response includes the portal's view of status
and the server time.

### GET /api/device/token

Auth: bearer. Returns the most recent unlock token the portal issued for this
device, so a device that is online can pull a token instead of the user typing it:

```json
{ "token": "20002-0F04J-CEBMA-PNWJB", "type": "full", "duration_days": 30,
  "expires_at": "2026-07-16T22:48:50+00:00" }
```

The token is applied through the same offline verifier, so an online pull and a
typed code follow the identical fail-closed path. 404 when no token exists yet.

## Enrollment code

An operator issues a code in the portal:

```
php artisan device:enroll-code ZG-40000
```

It is valid for 24 hours and single-use.

## Client side (built)

- `src/net/HttpClient` — WinHTTP JSON GET/POST with a bearer header (http and https).
- `src/net/Json` — a small JSON reader for the responses.
- `src/net/PortalClient` — `enroll`, `heartbeat`, `fetchToken`.
- The base URL lives in `DeviceConfig` (registry); the device token lives in the
  encrypted store (`StoredDevice.deviceToken`, format v2). The secret is never logged.
- Installer commands: `enroll --code <c> --url <u>` provisions from the portal,
  `heartbeat` checks in, `fetch-token` pulls a token and applies it through the
  offline verifier, `set-url` sets the base URL.
- `install` copies the installer to the program folder and registers a SYSTEM
  scheduled task (`Zaga Device Heartbeat`) that runs `heartbeat` hourly; `schedule`
  and `unschedule` manage it by hand. `uninstall` removes it.

Verified end to end against the running portal with the `net_host` harness: enroll
returns the bundle and token, heartbeat authenticates, fetch-token returns the
issued unlock code.

## Portal side (built, in the portal repo)

`routes/api.php` (`api.device.*`), `App\Http\Controllers\Api\DeviceApiController`,
`DeviceService::issueEnrollmentCode`, the `device:enroll-code` command, and the
`add_enrollment_to_devices_table` migration. `Device` uses Sanctum `HasApiTokens`.
