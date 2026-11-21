# Metalio control center V2

User approved `build/prototypes/metalio-apps-controls/control-v2.html` on
2026-10-26 with one change: remove the separate mute button below Volume.
The browser prototype and firmware implement that correction. Version stays 0.3.6.

The portrait canvas follows the approved 684x1216 geometry: sheet ends at y=918,
quick tiles at x=36/351,y=183,size=297x137; slider centers y=446/594/797 and
tracks x=122..562. Header time/network/battery replace the underlying page header.
Four-sample rounded shapes and generated 4-bit SVG glyphs keep curves consistent
with the approved prototype; no new runtime vector engine or allocation is used.
Reproduce glyphs with `native/generate_control_icons.cjs` and build-time sharp.

- Wi-Fi: tap toggles, long press or right chevron opens existing network settings;
  Back returns to the originating page with the control center reopened.
- Frontlight: switch retains prior mode/values; disabled sliders do not apply
  changes. Brightness +/- uses 5 points, cold/warm endpoints use 10 points.
- Volume: +/- uses 5 points and track reaches 0..100. No mute button or hidden
  mute target. Previously overloaded minus now always subtracts volume.
- Sliders commit through the existing release path and persist settings once per
  action. Keyboard, status refresh, panel power, voltages and charger unchanged.
- Upward close gesture, top close button and outside tap are retained. Landscape
  adapts controls into a right-hand column, preserving readable touch targets.
- Per the user's follow-up, open/adjust/close use a dedicated low-flash output
  path, including closing back to Reading. This prevents the reading transition
  flag from selecting a full black/white clean. Clear mode and a due periodic
  cleanup are deferred for this overlay operation; its successful frames still
  count toward the budget. The next ordinary frame may perform the due clean.
  First boot, explicit manual clean, sleep and draw-error recovery retain safety
  cleanup. "Low flash" remains a panel observation to verify, not a host claim.
- Active audio retains its return shortcut, expanding the portrait sheet only
  while audio is active. Existing upload/OTA/network and recording guards remain.

Native UI tests cover button/track endpoints, frontlight disabled state and value
retention, no hidden mute row, tile gutter, long press/chevron navigation and
return, closing/reopening gestures and landscape slider geometry. Reading menu,
font page and jump popup dismissals now select the same scoped overlay output;
refresh-policy changes do not silently promote those dismissals to full cleans.
The integrated 32/32 host suites passed. Final integrated build evidence is under
`build/bluetooth-20261026/`; `build/control-center-v2-20261026/` retains the earlier
standalone candidate. Native screenshots are host renders.

Installation pending: original COM5 (serial 1C:29:04:D0:4A:B0) is absent.
COM4 serial 14:C1:9F:D1:80:F0 differs and must not be substituted automatically.
Device/panel acceptance is separate from host checks. The prior low-flash
candidate remains without optical feedback; no new no-ghosting claim is made.
