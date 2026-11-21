# Bluetooth audio candidate (2026-10-27, version 0.3.6)

BLE connection/HID support added by the later 2026-11-17 user request is documented
in [BLE-CONNECTIONS-HID-20261117.md](BLE-CONNECTIONS-HID-20261117.md). The scan-only
scope below records the earlier discovery stage, which that later request expands.

The current Settings → Bluetooth entry merges audio and BLE discovery/saved peers;
see [BLUETOOTH-UNIFIED-UI-20261117.md](BLUETOOTH-UNIFIED-UI-20261117.md).

Implemented against the approved prototype: Settings entry, discovery,
pair/connect confirmation, saved devices, optional startup reconnect, disconnect
and forget. Unexpected loss pauses playback without speaker fallback.
The external BH1098G supplies Bluetooth, not an ESP-IDF on-chip Bluetooth stack.

## Source and ownership

Protocol and wiring reference: Metalio MIT source at
`b400605e9a6a276a2c39864c0cff63798c78e8d8`,
`main/display/screen/bluetooth_screen/bluetooth_uart.cc` and
`bluetooth_screen.cc`. Reference checkout unchanged. Parser, state machine,
UI and record storage are independently authored.

A single worker owns UART RX while Bluetooth is enabled. The main task owns
UI/display work. UART uses TX39/RX38; existing I2S uses 42/43/44/45. Mode changes
require exact acknowledgements and bounded timeouts. Connection success must
correlate to a requested device; device names cannot masquerade as status lines.
Audio scan results are bounded to 32 devices and saved records to four.

NVS namespace `thingsos-bt` stores a versioned CRC32 record with flags, saved
addresses/names and the last device. Records save only on confirmed connection.
Failed storage does not claim a durable record. Forget removes the host record;
it does not claim to erase undocumented module-owned bonding keys.

Route changes suspend I2S before switching the module. Software gates prevent
speaker output while Bluetooth is enabled or a route command is queued. Recording
requires Bluetooth off. Acknowledgement failure keeps the output gate closed.

## Validation boundary

Host tests execute the production parser and service with deterministic UART,
NVS and audio adapters: framing, injection rejection, bounds, mode timeouts,
correlated success, save/reload/CRC failure, storage failure and route gating.
Native UI tests exercise discovery, pairing, saved records, forget and switches.
All 32 host suites passed. These checks do not establish radio pairing or audio.

The intended COM5 / serial `1C:29:04:D0:4A:B0` was absent during integration.
COM4 / serial `14:C1:9F:D1:80:F0` is a different device and was not flashed.
Pending on the intended device: real headset discovery/pairing, BH1098G mode-2
PCM/I2S output, MP3/WAV playback, volume, disconnect pause, reboot persistence and
startup reconnect. This is a development candidate, not a hardware-accepted release.

## Discovery update, 2026-11-17 (version remains 0.2.0)

The user reports that a previously saved headset reconnects, but a new headset
in pairing mode is missing, and also requests discovery of BLE keyboards/page
turners. These are different discovery paths. The previous implementation only
used the external BH1098G audio inquiry; it had no BLE observer. Its single
18-second inquiry also stopped accepting results on the first completion, kept
at most 16 devices, and read only 128 UART bytes per worker iteration. These are
source-confirmed limits; they do not by themselves prove the RF cause for a
particular missing headset.

Audio inquiry now runs up to three rounds, preserving and deduplicating results
between rounds. Each round has a 25-second deadline. A missing completion uses
the already documented local/TX mode reset before a bounded retry. Explicit
search can supersede an in-progress reconnect. UART RX buffering is 4096 bytes;
the worker drains up to eight 512-byte batches per iteration and then yields to
commands. Saved NVS record layout and connection acknowledgement gates are
unchanged. The automatic-reconnect switch still concerns saved devices; finding
a new headset uses Search Devices. Unknown status lines are logged for diagnosis.

BLE uses a separate, independently written scan-only component on the ESP-IDF
NimBLE observer APIs and the installed ESP32-S31 controller library. Radio/host
initialization is lazy, at the user's first BLE search. Active discovery runs for
30 seconds, with unfiltered advertisements and scan responses, a 0x80 interval
and 0x60 window, and host duplicate filtering off so a response can supply a name.
The bounded list keeps public/random addresses distinct, retains complete names
when later packets omit them, rejects malformed AD lengths, and reports a full
list explicitly. The UI shows names, addresses and RSSI in a separate Low Energy
Devices page; it explicitly states that connection/key input is not supported
in this requested phase. BLE results never enter the audio connect/persist path.
Leaving the page stops discovery. No GATT, HID, bonding or pairing was added.

Regression tests exercise the production audio service with a saved reconnect,
24-device UART burst, a new device in a later inquiry round, bounded completion,
timeout retry and stop; BLE tests exercise name enrichment, address/type dedup,
malformed packets and saturation. Device UI tests cover scan-only presentation,
paging, stop/restart/back, and prevent BLE rows from requesting audio pairing.
Build, installation and physical acceptance evidence is recorded separately in
`build/font-bt-discovery-20261117/`. Host tests cannot establish radio discovery
of the user's actual headset or BLE peripheral.

### Captured automatic-inquiry root cause

The installed discovery candidate's `usb-install/main-boot.log` captured the
external module autonomously emitting `INQUIRING START` at 28.395 seconds and
`INQ COMPLETE` at 41.641 seconds, after saved-headset reconnect timeouts at
19.715 and 26.715 seconds. The host remained READY throughout. The service only
accepted device lines while SCANNING and had no SCAN_BEGIN handler, so any
results from this real automatic inquiry would be discarded. No nearby headset
results were present in that unattended capture; it proves the missing state
transition, not discovery of a particular missing headset.

The follow-up adopts the module's START while enabled, automatic mode is on,
the host is idle and no link is connected. It shows and accepts that module-owned
single inquiry without sending extra inquiry commands. Explicit three-round
search remains separate. Stop/disconnect suppress later unsolicited starts;
disabled automatic mode and a pending explicit connection are not overridden.
Regression tests replay this captured READY/START/DEVICE/COMPLETE sequence and
the suppression/disabled/connecting cases. Follow-up artifacts and installation
evidence are in `build/font-bt-auto-discovery-20261117-r2/`. The user reported the
device is away and will perform physical acceptance in the evening.
