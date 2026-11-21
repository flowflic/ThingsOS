# ThingsOS 0.2.2 — Metalio E-Ink4-Plus

Independent ESP-IDF firmware for the Metalio E-Ink4-Plus (ESP32-S31).

Version 0.2.2 fixes spontaneous restarts during standby (the BLE controller is
now stopped before light sleep and reconnects on wake), keeps BLE memory
reserved so page turners connect after audio playback, follows Chinese line
breaking rules with justified body text, adds the read-only flomo app, splits
Cubox into recent articles and folders, and redesigns the wallpaper app.
See [release notes](native/docs/METALIO-0.2.2.md).
The application lives in `native/`; the project and default main/rescue firmware
version is defined by [`native/VERSION`](native/VERSION).

The pending offline WeRead reading-time feature has a separate E-Ink
authorization and durable outbox. Usage, recovery and validation boundaries:
[`WEREAD-OFFLINE-READTIME.md`](native/docs/WEREAD-OFFLINE-READTIME.md).
The adapted protocol carries its original copyleft licenses; see
[`READTIME-SOURCES`](licenses/READTIME-SOURCES.md).

```powershell
./native/build.ps1 -Rescue
./native/build.ps1
python ./native/package_metalio.py
```

Builds and packages are written under `build/`. These commands do not flash a
device or publish a release. Explicit device authorization is required.
Version 0.1.4 adds the fixes made after the 0.1.3 publication: the update check
pre-warms the release host after DHCP, connects with a five-second timeout and
retries up to three times, the status bar shows a bolt while the PMU reports
charging, the EPUB manifest ceiling rises to 2048 entries with the TOC reorder
buffer in PSRAM, and WiFi connect logging is quieted.
It retains the 0.1.3 clock fallback, brightness, panel rail and cover-retry
work, the 0.1.2 large-shelf/login, prefetch, TOC timeout and WiFi wake fixes,
plus the keyboard fixes and one-shot/date-range alarms.
Build and package outputs are written under `build/native-usb/`.
On 2026-11-02 the device was updated to a
later local 0.1.4 candidate with the internal charging bolt, charge detection
fixes and WiFi policy changes. Full readback and startup health passed; the
user confirmed the charging icon and unplug/replug behavior. This candidate
has not replaced the published package. See the
[device test record](native/docs/CHARGING-WIFI-DEVICE-20261102.md).

A subsequent local 0.1.4 candidate fixes EPUB inline-image decoding under
fragmented PSRAM and restores immediate rail shutdown after reader updates.
It has passed full readback and startup health. The user confirmed image
rendering, speed, static-page behavior, body-width sizing and long-image panning.
The bounded on-device results are tracked in the
[reader fix record](native/docs/EPUB-INLINE-IMAGES-20261102.md).

The WeRead cache failure for long text metadata is traced to the per-string
JSON limit. A bounded metadata scan and regression tests are recorded in the
[WeRead investigation](native/docs/WEREAD-LONG-CONTENT-20261102.md).

The next local candidate adds on-demand WeRead chapters with three-chapter
prefetch and removes the packer's 512-chapter policy limit. Source, boundaries
and validation are tracked in the
[chapter-reading record](native/docs/WEREAD-ONLINE-20261102.md).
Long-press menus retain whole-book caching alongside reading and notes. The
follow-up [cache/cancellation record](native/docs/WEREAD-CACHE-CANCEL-20261103.md)
covers bounded network waits, cancellation checkpoints and local packing measurements.

See [0.1.4 version notes](native/docs/METALIO-0.1.4.md),
[application documentation](native/README.md),
[board mapping and acceptance list](native/docs/METALIO-PORT.md), and
[upstream provenance](upstream-sources.json). Reference checkouts remain read-only;
upstream licenses and notices are retained.
