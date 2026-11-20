# UI tab residue and approved app icons

2026-10-26, version remains 0.3.6. Firmware candidate; optical acceptance is separate.

## Separate observations

The user reported dots after several home-tab switches, appearing after a pause
and disappearing after manual full refresh. During a 150-second passive capture,
12 successful refreshes were recorded: manual GC16 clean at uptime 517331 ms,
several trimmed page updates around 528 ms each, last navigation to Settings
ending 538992 ms, and a status-only update ending 542660 ms (658 ms). All 12
updates read back panel outputs disabled. There was no underrun, draw failure,
panic or assertion. This is temporal evidence, not proof that the status update
caused the visible dots. Log: build/ui-static-speckle-20261026/live-before.log.

The user separately clarified that the background under frontlight also looks
uniformly gray-white, stays that way with the light off, and has no distinct dots
after full refresh. Do not equate this with accumulated residue, or promise that
changing software white or increasing refreshes will produce paper-white optics.
The software canvas is white (255), and packing all-white input produces only
0xff in grayscale and fast paths. Frontlight uses the existing 20 kHz PWM and
vendor half-duty ceiling; no frontlight, VCOM voltage, EEPROM or charger change
is made in this candidate. A physical white-state/illumination comparison is
still needed before attributing uniform gray-white to hardware or calibration.

## Confirmed source mismatch and candidate

- Home navigation calls thingsos_board_cleanup_area, which was a no-op.
- Balanced non-reading pages were passed through all-pixel GC16 using the Pico
  36-phase trim, even though the normal log incorrectly labelled that as GL16.
  Manual clean uses the complete 48-phase table. Same-pixel coverage alone does
  not preserve the complete erase sequence. The old counter also counted these
  complete UI redraws as partial frames, contrary to its own comment.
- Queue the tab normalization until the next full UI frame, and use the complete
  Metalio GC16 table for non-reader UI transitions/redraws. Reset its partial-frame
  budget after a successful complete redraw. Consume the pending tab request only
  on an eligible successful frame. No extra redraw pass or delayed timer is added.
- Reader DU20 white maintenance, reader text/stagger and keyboard trim retain their
  existing tables. Audio timers, progress and status-only windows keep their scopes.
  Log actual UI phase count, tab cleanup request and temperature for device checks.
- This does not make manual GC16 redraw equivalent to the physical epd_clear used
  at startup, sleep and recovery. Do not silently add a physical clear per Tab.

## Icons and navigation

The user asked to match the previously approved app prototype. The former C
hand-drawn icons had square frames, rough Wi-Fi/microphone curves and mismatched
icon/label centers. Firmware now uses the approved SVG paths, rounded 108px tiles,
57px glyph geometry, 1.35-unit round strokes, and 4-bit grayscale assets. Locations
match the prototype: icon x=78/288/498, rows y=150/383. Name labels share each
icon's center. Touch bounds exclude the gaps. No title, swipe hint or SD capacity
is added to the launcher. The frontlight icon uses the sun rather than a music note.

Source: firmware/main/assets/app-icons.json. Reproduce app_icons.inc with
generate_app_icons.cjs using sharp at build time; there is no SVG/runtime dependency
on the device. Total icon flash data: 15924 bytes. All paths come from the approved
local prototype and are independent of legacy/reference firmware.

File management always selects Applications in the bottom navigation, including
its file-search editor, nested folders and file preview. Settings/Library entries
remain available through their existing navigation.

## Verification and acceptance

29 host suites cover all 48x256 UI actions against the unmodified full reference
table, unchanged reader/key selection, pending cleanup exclusions, white packing,
native rounded icon rendering, gap hit-testing and the Applications underline for
file management/search. Native previews and logs live in the build folder above.
Main/rescue builds, installation hashes and actual panel acceptance are recorded
separately. After installation, repeat the same tab sequence, stop on Settings,
and observe at least two minutes including a status refresh, holding illumination
constant. Dots must remain absent; uniform gray-white is a separate observation.

2026-10-26 installation: candidate SHA256
`3e9e0fe545454357203c3adfd8e5792b0ec754497066ef82f24e783b4498c882`
(6,438,272 bytes, version remains 0.3.6) was installed through the existing rescue
partition on COM5. Full flash backup, main/selector readback, protected-region and
NVS comparisons passed. Main startup health confirmed; boot logs recorded full
48-phase UI refresh, result 0 and verified panel output-off, without underruns or
display errors. Evidence: `build/ui-static-speckle-20261026/device-flash/`.
User confirmation of icon appearance, navigation and idle optical behavior remains
pending; startup checks do not establish panel acceptance.

## Follow-up: reading-style low-flash UI navigation

The user subsequently confirmed no problems with that installed candidate,
including icons/navigation and idle dots, but reported excessive flashing during
home-tab and nested-menu changes. This validates the prior observation only for
that sample; it does not establish that a weaker waveform is also artifact-free.

Ordinary non-reading, non-keyboard, non-status UI frames now reuse the reading
waveforms: balanced mode uses the reader grayscale text table with its single
white-going maintenance tick, and fast mode uses the reader DU20 table with its
two stable-white maintenance ticks. Both are delivered over all pixels, including
unchanged white, rather than a pure difference mask. This applies to both tabs
and nested page transitions. Balanced mode keeps grayscale icon antialiasing.

First boot, explicit full refresh, configured clear mode, periodic cleaning,
wallpaper suspend and draw-error recovery retain complete cleanup. UI low-flash
transitions increment the existing partial-frame budget instead of resetting it
on every navigation, so the configured periodic clean still occurs. Pending tab
requests are consumed only after successful eligible updates. Missing text LUT
falls back to the complete table. Panel power-off/readback is unchanged.

Follow-up evidence lives in `build/ui-low-flash-20261026/`. Verify ordinary
navigation reports `UI low-flash`, inspect clean/partial budget progression, then
repeat tab/nested-menu changes and idle on Settings for at least two minutes.
The prior user's dot-free acceptance does not cover this new candidate.

Follow-up host/build/install checks: 29/29 suites passed after the parallel
builds completed (the initial timing-sensitive reader worker assertion and its
successful rerun are retained in both logs). Main/rescue builds completed. The
new main image is 6,438,592 bytes, SHA256
`f3b19043cc391c88d882b235aff72b30a48705703cf4dca29dddcfeb44c024b5`.
Installation through the existing rescue partition, full readback, selector,
protected regions and unchanged NVS checks passed. Startup health confirmed and
`UI low-flash GC16` returned result 0. Optical feedback remains pending.
