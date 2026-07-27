# Fleet firmware detection + HTTP OTA — Design

Date: 2026-07-27
Status: Approved (interactive Q&A: HTTP /api/update transport; device-served
/update form page).

## Goal

Detect each device's firmware version, know the *available* version from a
built image, and push OTA updates to all detected devices — from the host CLI
today, and from a browser or the future Android app (which cannot speak
espota) tomorrow.

## What already exists (verified)

- Every heartbeat and `/api/status` reports `fw=` (`kFirmwareVersion`).
- Gap 1: the constant was never bumped (chase firmware still says `2.0.0`),
  so wire versions can't distinguish builds. Fix + adopt discipline: **bump
  the version on every behavioural firmware change** (start: `2.1.0`).
- Gap 2: the only OTA client is python espota (ArduinoOTA); unusable from
  web/Android. ArduinoOTA stays for PlatformIO dev flows.

## Design

### Versioning (single source)

`src/matrix_main.cpp`:

```cpp
#define LT_FW_VERSION "2.1.0"
static const char *kFirmwareVersion = LT_FW_VERSION;
// Embedded so the host can read a .bin's version by scanning for the marker.
static const char kFwMarker[] = "LTFW:" LT_FW_VERSION;
```

The marker must be referenced (printed once at boot) so the linker keeps it.
"Available version" = scan the `firmware.bin` bytes for `LTFW:` and read the
semver that follows (NUL/non-printable terminated). Chosen over the ESP-IDF
app descriptor because the precompiled Arduino core controls that field; a
marker scan is toolchain-proof and unit-testable.

### Device: HTTP update endpoint + page

- `POST /api/update` — raw `application/octet-stream` firmware image via the
  WebServer upload handler: `Update.begin(UPDATE_SIZE_UNKNOWN)` → `write` →
  `end(true)`; on success reply `{"ok":true,"version":"<running>"}` then
  reboot (~1 s delay); on failure `{"error":"<Update.errorString>"}` + 500 and
  NO reboot (old firmware keeps running — Update.h only commits a fully
  written, verified image).
- `GET /update` — minimal static HTML form (`<input type=file>` POSTing to
  `/api/update`) so any browser can flash a single board tool-free.
- LAN-trusted, no auth (consistent with the rest of the REST surface).

### Host: fleet updater (in LaserTag.Client so the Android app reuses it)

- `FirmwareImage.TryReadVersion(path)` — the marker scan.
- `FirmwareUpdater.UploadAsync(ip, binPath, ct)` — HttpClient POST; returns
  ok/error + device-reported version; generous timeout (flash write ~10-30 s).
- REPL:
  - `fw [path]` — table per roster device: id, host, ip, running fw,
    available fw, verdict (current / **outdated** / newer / unknown). Default
    path: `.pio/build/esp32-s3-matrix-ota/firmware.bin`, falling back to the
    non-ota env's bin.
  - `ota <id>|all [--force] [path]` — sequential HTTP pushes to online
    devices; `all` targets those with running ≠ available (`--force` pushes
    regardless, e.g. re-verify); per-device result lines. Version comparison
    via `System.Version` (semver x.y.z).
- Boards on pre-2.1.0 firmware have no `/api/update` (404) — `ota` reports
  the failure and the fix (one last espota flash); their `fw` verdict still
  shows outdated correctly via HB.

## Testing

- xUnit: marker scan (found / missing / truncated-at-EOF / non-printable
  terminator), version compare verdicts, `FirmwareUpdater` against a loopback
  `HttpListener` stub (success, 500, timeout).
- Firmware: build gate + live bench (flash 2.1.0 via espota one last time,
  then verify `fw` table shows 2.1.0 current, and prove the HTTP path by
  `ota <id> --force`).

## Out of scope

- Auth on the update endpoint; delta/compressed updates; espota C# client;
  the Android app UI itself (this lands the reusable client pieces).
