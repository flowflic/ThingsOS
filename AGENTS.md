# ThingsOS development

ThingsOS is an independent ESP-IDF v6.1 firmware for Read Pico RDP-G01-W.
Read upstream-sources.json and README.md for provenance and build commands.

This tree (D:\ThingsOS_nologotech, 2026-10-23) is the port of ThingsOS 0.3.6 to the
Metalio E-Ink4-Plus (ESP32-S31). See native/docs/METALIO-PORT.md for the board
mapping, build/packaging and the on-device acceptance list. The Read Pico tree in
D:\crossmux\ThingsOS stays the source of truth for shared application changes.
- Metalio board drivers live in native/metalio/components (metalio_bsp, epdiy fork).
  Metalio-derived code keeps its MIT notice; references/metalio-e-ink4-plus is read-only.
- Never write the TPS65185 VCOM EEPROM (PROG bit) or change the charger safety policy.
- The device COM port must not be flashed without explicit user authorization.

- User decision (2026-10-11): do not base the replacement firmware on kiikoread /
  read-pico-reader. The existing main/ and components/ tree is legacy code, not
  evidence of an independent implementation. Do not continue skinning it.
- Implement the replacement in native/. Its application, input, reader, storage,
  network and OTA code must be independently written. Official Read Pico BSP and
  independently identified upstream libraries may be used with licenses intact.
- Do not copy modified hardware drivers from the legacy tree. Select hardware
  components from references/read-pico-official with explicit provenance.
- A matching official/reference file is not proof of copying from kiikoread;
  record shared origin. Modified files require review, not an independence claim.
- Keep the published 0.1.1 and its historical source notices accurate. Do not
  publish the replacement or label it flash-ready until all required features,
  source boundaries and recovery paths have been validated.

- The approved product uses one combined EPUB/TXT library and fonts in /sdcard/fonts.
- User decision (2026-10-12): built-in Example.txt is visible/openable only in the
  file browser, including without SD. Do not show it in library/recent or as a
  bottom shortcut. Keep its embedded Example Test content read-only.
- Keep PMU VCOM factory-owned and never expose or change panel voltage settings.
- Retain the official target-specific timing and BSP safety checks.
- Never overwrite the running application partition. Verify OTA before selecting a boot slot.
- Device/UI ownership stays on the main task; do not scan the display concurrently with OTA writes.
- Keep host tests and on-device evidence distinct. Do not flash or publish without authorization.
- Preserve upstream SPDX notices and component licenses. Do not change reference checkouts.
- Use build/ for artifacts. Do not commit unless requested.

- User version policy (2026-10-12): preserve the current version on routine fixes and builds. Change version numbers only when the user explicitly requests a version upgrade.

## Metalio USB flashing workflow (validated 2026-10-31)

- Before any Metalio USB install or recovery, read `native/docs/METALIO-USB-FLASH-PROCEDURE.md`. It records the successful ESP32-S31 connection, backup, rescue, verification and boot-health sequence. Explicit user authorization is still required.
- Prefer the validated persistent serial session: set DTR/RTS before opening, promptly sync ROM, handle the documented volatile S31 download watchdog state, then load the Espressif stub. The Windows timeout-only serial wrapper and exact register sequence are in the document; do not generalize them to other chips or blame every connection failure on the USB cable.
- Back up the full 16 MiB Flash. Before updating a healthy main application, actually boot and verify rescue; never write the running application partition. Verify the application and preserved regions by readback before selecting the main boot slot, then verify the selector itself.
- Capture 45 seconds after rescue/main startup. Confirm the candidate ELF, library UI and startup health, and close the serial session before handing the device back. Do not repeatedly reset before health confirmation or access the port concurrently with the website flasher.
- User decision (2026-11-15): prevent recurring missing early ELF logs. Open and arm serial capture before triggering a boot; retain the same session across reset where possible and capture from the first boot bytes, not after a post-reset delay. Before flashing, identify the installed application's ELF/version/slot from the last verified installation and cross-check against the full Flash backup's app descriptor; do not reuse a stale script baseline. USB enumeration may omit early output: a missing `ELF file SHA256` line is missing evidence, not a firmware fault. Preserve strict ELF/slot/hash gates; never delete or weaken them to pass. If early output is missing, finish the current 45-second health capture first, then use a bounded capture retry as documented in the procedure; if it still fails, stop before device writes. Record which evidence established the baseline and final boot identity; never claim a missing log line was observed.
- The old `native/flash-metalio.ps1` still uses `--no-stub` for large backup reads and is not the validated workflow. Successful scripts under `build/` are one-off records with fixed hashes, baseline checks and output paths; inspect/adapt their gates before reuse. Interrupted rescue OTA may leave an incomplete main image; do not claim the old application is intact without verification.
