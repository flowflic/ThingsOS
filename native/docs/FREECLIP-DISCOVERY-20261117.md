# FreeClip discovery investigation (0.2.0)

The user reports HUAWEI FreeClip is visible in Windows Add Device but absent
from the installed firmware's search. The screenshot establishes Windows
discovery; it does not contain BH1098G inquiry output for this headset.

## Confirmed interference in existing device capture

`build/ble-sram-crash-20261117/fix/retest/repro-live.log` records a manual search
at 309055 ms, MODE 2 acknowledgement at 312539 ms, and INQUIRING START at
312651 ms. The same search then receives CONNECT TIMEOUT at 322689 ms,
RECONNECT DEVICE NAME:OPPO Enco Free4 at 322797 ms, another INQUIRING START at
329706 ms and another CONNECT TIMEOUT at 333201 ms. The module continues its
own saved-device reconnection after acknowledging MODE 2. The host's immediate
inquiry is therefore not an isolated discovery operation. Resetting modes on
each timeout restarts the same interfering sequence.

This is a confirmed search sequencing defect and a plausible cause of the
reported missing headset. There is no captured FreeClip result to establish
that it is the sole cause or to claim physical compatibility acceptance.

## Change

After entering transmitter mode for explicit search, keep the UI in SCANNING
and wait up to 20 seconds for the module's own fallback INQUIRING START.
Adopt that as round one, retain its device results, and issue subsequent inquiry
rounds only after completion. If no START arrives, issue the explicit inquiry
at the bounded deadline. Ignore completion before a START while waiting.
Timeout recovery also waits for fallback after resetting modes, instead of
immediately overlapping the module's reconnect procedure again. Stop, connect
and power commands retain their existing priority and acknowledgement gates.

Host tests replay reconnect timeouts, automatic START, completion, subsequent
rounds, missing START, timeout recovery and a synthetic FreeClip UART result.
Synthetic packets verify parser/service behavior, not radio discovery.

Evidence is stored in `build/freeclip-discovery-20261117/`. Version remains
0.2.0. No device flashing, publication or commit is authorized by this task.
Actual FreeClip discovery, pairing and audio remain pending device validation.

Final validation: 6/6 selected host tests passed, including the production
Bluetooth service, UART parser, BLE discovery/lifecycle and device UI suite.
ESP32-S31 firmware build and application JPEG symbol checks passed. The normal
sandbox failed to launch tool binaries with Windows DLL relocation errors;
running the same build script outside that sandbox succeeded. Candidate BIN
and ELF are archived in the evidence directory above. Nothing was flashed.

## Authorized installation

The user subsequently authorized flashing ("烧录"). Installation completed in
`build/freeclip-discovery-20261117/usb-install-attempt2/result.json` on COM5,
USB serial `1C:29:04:D0:4A:B0`. The initial sandbox attempt could not open the
port and made no device writes; its evidence remains in `usb-install/`.

Baseline ELF `4912acb3917eae5cfb553274f17a60095e589c54aa5f3bbd3bd669168d7d75cb`
was established from the latest bt-ble-names installation record, armed early
boot capture, and the current full Flash backup descriptor/byte comparison.
Rescue actually booted and passed its 45-second capture before main writes.
Full 16 MiB readback and selector verification passed; NVS matched the original
backup and all protected regions were preserved.

Installed BIN SHA-256:
`88b5c31711e7f7ad1d6311aa5c9e307ba6a4b2e519849c230235bdc4dee01666`.
Installed ELF:
`5b67f35b46c1c7e6f2cf2055fd127f8998c6c2f20b5e278d6ceab7af14094d81`.
Final boot captured the matching ELF line, main slot `0x220000`, version 0.2.0,
library UI and startup health confirmation at 37518 ms. No checked boot errors
were observed during the 45-second capture. Serial was closed. Actual FreeClip
discovery, pairing and audio remain pending the user's physical test.

## Physical feedback and address identity follow-up

After installation the user reported no FreeClip name and reproduced a manual
search while a no-reset serial capture was running. The capture in
`build/freeclip-live-20261117/live.log` shows fallback inquiry at 336430 ms,
one accepted audio device at 349750 ms and completion of three rounds at
377680 ms. The user identified its displayed name as `10973779E5C0`.

A read-only Windows Bluetooth inquiry (no pairing or connection) independently
returned `HUAWEI FreeClip`, address `C0E579379710`, class `0x240418`, disconnected.
See `build/freeclip-live-20261117/windows-inquiry-outside.json`. Reversing the
six address bytes exactly matches the audio row. The headset was discovered;
the missing name and wire-order address made it unrecognizable. The user then
selected that row and reported connection failure. This supersedes the earlier
hypothesis that inquiry/reconnect overlap explained this particular report.

Follow-up candidate (not yet installed):

- Display BH audio addresses in standard byte order with colons; retain the
  original wire address in commands, device records and identity comparisons.
  BLE addresses are not reversed. Nameless audio rows say 未命名音频设备.
- User explicitly requested audio-first ordering. Late audio results insert
  before BLE rows, retaining discovery order inside each transport.
- On explicit connection failure/timeout, try the byte-reversed address once;
  preserve the original discovery identity on success. Both attempts retain
  success-acknowledgement gates and bounded timeouts. This is a compatibility
  experiment pending physical validation, not a proven connection root cause.
- Record all parsed UART lines including device payloads, plus actual inquiry
  and connect commands, so subsequent tests can distinguish naming from radio
  and connection failures.

Candidate evidence: `build/freeclip-diagnostics-20261117/`. No model-specific
name or MAC is hardcoded. The latest installed firmware remains the earlier
`5b67f35b4...` ELF until a subsequent installation is explicitly recorded.

## Official-flow correction (supersedes experimental address retry)

At the user's request, reviewed the pinned read-only official reference:
`references/metalio-e-ink4-plus/main/display/screen/bluetooth_screen/bluetooth_uart.cc`
and `bluetooth_screen.cc` (upstream commit in `upstream-sources.json`). The
reference parses the first 12 hex digits after `AT+BT:` as the opaque address
and the remaining text as name. Its mode-2 scan button sends `AT+INQUIRING`
directly. Its device-row click sends `AT+CONNECT=<returned address>` directly,
without switching modes and without reversing the address. It contains no
separate remote-name query; an empty name is displayed as an address there too.

ThingsOS previously re-entered local/TX modes before every connection. That
starts the module's old-headset reconnect sequence again immediately before
the selected-device command. The revised independent worker tracks confirmed
TX mode and sends search/connect commands directly while already in that mode.
Mode changes remain necessary for startup, disconnecting an existing link,
interrupting a previous pending connection for search, and timeout recovery.
Existing acknowledgement, main-task UI ownership and durable-record gates
remain. Device results continue to merge across the bounded inquiry rounds.

Removed the speculative byte-reversed connection retry from the uninstalled
candidate: official source provides no basis for it. Display-only standard
address formatting, nameless-audio labels, audio-first rows and complete UART
diagnostics remain. No FreeClip name/MAC is hardcoded. Tests assert search and
connection from existing mode 2 send no MODE commands and preserve the exact
returned address; timeout cannot send an alternate address. This aligns host
command sequencing with the official implementation; actual FreeClip name
availability and connection success still require physical validation.

Final candidate evidence: `build/freeclip-official-flow-20261117/`.
Selected host regressions passed 6/6; ESP32-S31 build and application JPEG
symbol verification passed. BIN/ELF and descriptor/hash validation are archived
in that directory (`validation.json`). This official-flow candidate has not
been installed. Version remains 0.2.0.

## Official-flow candidate installed (user authorized)

Installed the frozen 22:58:39 candidate after the user explicitly requested
flashing and excluding concurrent WeRead menu-ghosting work. The excluded
chat began at 23:00:06, after this archived BIN/ELF were built; no rebuild from
the concurrent worktree was performed. See
`build/freeclip-official-flow-20261117/scope-exclusion.json`.

`build/freeclip-official-flow-20261117/usb-install/result.json` records full
16 MiB backup/readback, verified rescue boot, preserved NVS/protected regions,
verified boot selector, and final 45-second healthy startup. Main boot ELF
`43dec4acf...` was captured at startup, slot `0x220000`, version 0.2.0, library
ready and startup health at 37517 ms. Serial closed. Actual FreeClip pairing
and audio still require physical validation; this installation does not claim
that the missing remote name or connection failure is hardware-accepted.
