# ThingsOS 0.3.6 on Metalio E-Ink4-Plus

Latest reader investigation: [static-page speckles and low-flash candidate](READER-STATIC-SPECKLE.md)
(2026-10-25). The preceding full white-reset candidate did not solve static-page
speckles and was withdrawn. VCOM enable is now stable through an update and
panel rails are closed immediately afterward, following the Metalio reference.
Older keepalive and per-phase VCOM-gating descriptions below are historical.

Latest application baseline: **Read Pico 0.3.8 (`a509301`)**, ported 2026-10-25.
Metalio version identifiers remain 0.3.6. See [PICO-038-PARITY.md](PICO-038-PARITY.md)
for this migration and its validation. The older sections below describe earlier
porting work; device observations there do not validate this new candidate.

Status (2026-10-25): main firmware, rescue firmware and the merged USB image
build for ESP32-S31 with ESP-IDF v6.1; 26/26 host tests pass. **Nothing has run
on a device yet.** Do not publish or label this build flash-ready until the
acceptance list below is done.

## Application-layer parity with the Read Pico tree (2026-10-24)

The shared application layer was re-based on the uncommitted work in
`D:\crossmux\ThingsOS` (ThingsOS 0.3.6 plus its pending application changes) so
that both devices ship the same UI, interactions and features. Only the board
layer stays device-specific. The port used a three-way merge against the v0.3.6
commit, so Metalio-only hunks survived: `board.c`/`board.h` (touch, power,
frontlight, SD, refresh tuning), `scan_adapter.c`, `stagger.inc`,
`sleep_wait.inc`, `usb_storage.c`, `tusb_config.h`, the board CMake files and
every `native/metalio` driver.

Carried over from Read Pico:

| Area | What is now identical |
| --- | --- |
| Annotations | Long-press selection, underline plus up-to-1024-byte thought, list/detail cards, edit and confirmed delete, per-book/per-account snapshot with CRC and backup rollback (`src/annotations.c`, `main/annotations_ui.inc`, `weidu_annotations.inc`) |
| WeRead notes | Offline personal-note list and refresh, manual "sync now", automatic versus manual sync policy moved into Reader → More, server round-trip verification before commit |
| File browser | 8-row cover-less list, in-directory keyword filter, paging and rail position, 512-entry directory model, open/delete/cancel with a second confirmation, transactional delete (`main/files_ui.inc`, `src/files.c`) |
| TOC / chapters | Navigation loaded separately from reading segments, titles completed on entry, offline sub-section recovery for cached WeRead books, `navigation.cache` bound to the packed text CRC |
| IME | 60,000 offline words from pinned MIT jieba frequencies and pypinyin readings, full-syllable spelling, `'` disambiguation, contextual continuation, three wide candidates, 64-character composition (`src/ime.c`, `src/ime_words.inc`) |
| Reader / network | WeRead cache naming with the `.weidu-names` map, WiFi "disconnect"/"reconnect", idle-gated background refresh, partial refresh for home tabs and library filters |
| Tilt | Wrist-phase state machine in the input task with one bounded intent, posture allowance in the recentre rule, 140 ms recentre gate after release |
| Tests | New `annotations_snapshot_recovery` and `offline_pinyin_words` suites; the ported UI controller covers the annotation, sync and file flows |

Deliberate capability differences, not behaviour drift:

- **Home cover cleanup.** `thingsos_board_cleanup_area()` is an explicit no-op
  here. That hint exists because the Read Pico tuning can leave equal white
  pixels undriven, so its home cover keeps history. Every non-reader frame on
  this panel is driven all-pixel (full-pixel GL16, or GC16), which normalises
  the background anyway; an extra area scan would only add a flash.
- **Frontlight and haptics.** Metalio keeps its own Display → 前光与触感 page
  (off/warm/cool/both, 10–100 %, key vibration), NVS keys and boot restore.
  Read Pico has no such hardware, so its tree has no equivalent page.
- **Volume keys.** Metalio keeps Vol+/Vol− as previous/next page and the bezel
  HOME/PREV/NEXT mapping onto the key band. The shared key handler calls the
  board hook, so both trees run the same UI code.
- **Waveform, VCOM, rails, power-off, sleep wake.** Untouched; see the tables
  below. The new app code changed no table, timing constant or voltage.
- **Release tooling.** `package_beta.py`, `capture_beta_log.ps1` and
  `beta-testing.md` from the Read Pico tree are not copied: Metalio has no
  release channel and `package_metalio.py` owns packaging. The offline lexicon
  generator (`generate_ime_words.py`) and its pinned vendor data are copied so
  `ime_words.inc` stays reproducible.

Verification: `native/build.ps1` and `native/build.ps1 -Rescue` succeed (main
6,266,448 bytes in the 11.4 MiB `ota_0`, rescue 1,337,088 bytes in 2 MiB), host
tests are 26/26 (the new `lut_1ppb_page_lookup` covers the page builder and the
line converter), and `generate_ime_words.py --check` reproduces
`src/ime_words.inc`. Nothing was flashed, published or version-bumped. Device
acceptance still has to cover the new annotation, file-browser and IME screens
on this panel, and the `ThingsOS_perf` timings above were measured on the Read
Pico panel.

## Page turns: trimmed 36-phase table (2026-10-25)

Reading page turns are the frame the reader waits for, and on this board they run
through the masked/text views rather than the page-change branch. Those views are
now built from the supplier's trimmed table (`E0470_WAVEFORM`, GC16 36 / GL16 37)
instead of the untrimmed 48-phase set, and a page change uses the same trimmed
table. Clean, periodic and recovery frames still drive the untrimmed 48 phases, so
the erase and saturation margin is spent where residue is actually removed.

Measured on the device (10 MHz, one page):

| Frame | Before | After |
| --- | --- | --- |
| Reading page turn (masked GC16) | 1272 ms | **966 ms** |
| Page change (transition GC16) | 1272 ms | 966 ms |
| Periodic clean (15 turns, untrimmed) | 1274 ms | 1274 ms |
| Menu tap (DU 20) | 518 ms | 518 ms |

Why 10 MHz: the line feeder could not sustain 20 MHz on this fork before the
2026-10-25 page-table/mask change (see the line-feeder section below, where the
clock now holds). A retry at the validated clock used to underrun within ~100 ms
even with Wi-Fi idle, so the board latched the floor instead of paying a 3.4 s
physical clear per retry. `clock_retry_step()` stays in `board.c` as the retry
hook; it is not reached while the feeder keeps up.

## Line feeder: vendor 2.0.76 policy, and why 20 MHz still fails (2026-10-25)

The vendor sources document the underrun as a feeder problem, never as a clock
problem: `display-debug.md` records that Pico's smaller prefill underruns on the
S31 (2.0.64), that 2.0.66 had to wait for the whole prefill prefix before starting
the scan, and `render_lcd.c` states plainly that "S31 uses C LUT feeding instead of
Pico's Xtensa SIMD" and that the 64-line profile underruns with concurrent audio.
Their answer is 192 rows of prefill with fixed per-producer row ownership, and the
doc still admits (`display-debug.md:239`) that concurrent voice + refresh produced
`0x400` even then.

Ported into `scan_adapter.c` (our replacement for the fork's `render_lcd.c`):
fixed ownership (`l = thread_id; l += NUM_RENDER_THREADS`), a full-prefix commit
wait with a 2 s deadline that reports `0x400` instead of starting with holes, and
`vTaskDelay(0)` plus an iteration cap in the queue-wait loops.

Measured on this board:

| Configuration | Page turn | 20 MHz outcome |
| --- | --- | --- |
| 127 prefill, dynamic claims (previous) | 966 ms | underruns within ~100 ms |
| 192 prefill, fixed ownership, prefix wait | 1014 ms | underruns within **26 ms** |
| 127 prefill, fixed ownership, prefix wait (current) | **966 ms** | latched to 10 MHz |

Conclusion: prefill depth is not the limiter here. 20 MHz needs one 684-pixel line
every 11 µs (~62 px/µs); this C line path converts ~34 px/µs (measured: a 192-row
buffer drains inside a single 13.6 ms frame), while 10 MHz asks for ~34 px/µs and
is therefore stable. 192 rows also cost ~48 ms per turn at 10 MHz, so the board
keeps 127 while the fixed ownership and prefix commit stay.

Two ways forward, both product decisions rather than bug fixes:

1. make the line path ~2x faster (fold `epd_output_reverse_pixels` into the LUT
   build, revisit the per-line mask pass), then retry 20 MHz for 36 phases at
   ~0.49 s per turn;
2. follow the vendor reader instead: plain binary text pages use DU (20 phases),
   which at 10 MHz is 20 x 24.66 ms ≈ 0.49 s with no driver change, at the cost
   of anti-aliased grayscale text.

## Line path measured per line (2026-10-25)

Underrun diagnostics now log the vendor's fields (`phase`, `prefill`,
`first_missing_row`, `reason`, `consumed/total`) plus a per-frame cycle split of
the line path. First boot at 20 MHz on the current build:

```
epd_feed: phase=0 prefill=127 first_missing_row=488 reason=isr-empty consumed=488/688
epd_feed: line path: lut=11074 mask=580 reverse=1214 cycles/line over 684 lines
```

Board timing for this panel: `scan full 20 MHz: line 17us, frame 11917us` and
`scan full 10 MHz: line 33us, frame 23133us` (684 gate lines x 1216 px). Two
producers share the work, so the per-line budget is twice the line time:
20 MHz gives 34 us (10 880 cycles at the 320 MHz CPU), 10 MHz gives 66 us
(21 120 cycles).

| Component | Cycles/line | Share | Note |
| --- | --- | --- | --- |
| LUT pixel conversion | 11 074 | 86% | plain C, no PIE vector unit |
| Line mask | 580 | 4.5% | skipped when the frame mask is all ones |
| Pixel reverse | 1 214 | 9.4% | SWAR now (was a 256-byte table lookup) |
| **Total** | **12 868** | | 18% over the 20 MHz budget |

So the queue drains at 20 MHz after ~488 of 688 lines: the feeder is not 3x too
slow, it is about 18% short. Folding the reverse into the LUT saves 9.4%, and
skipping the mask on full frames another 4.5%; together that lands at ~11 100
cycles, which is only just above the 10 880 budget. Holding 20 MHz with margin
needs the conversion itself to shrink (vector path or a wider table), which is
where the vendor's `experimental_pie_pixels` build differs from this C path.

## Line feeder: full-frame mask skip and page-table 1ppB lookup (2026-10-25)

Both items from the list above are implemented, in the order they were costed:

1. **The line mask is skipped when the frame is full.** `epd_draw_base()` now
   records `RenderContext_t.line_mask_full` (one word-at-a-time pass over the
   304-byte mask, once per frame) and `lcd_calculate_frame()` only runs
   `epd_apply_line_mask_VE()` when the mask can actually clear a byte. Turns
   force `dirty_columns` to 0xFF in `stagger.inc`, so every page turn takes this
   path: −580 cycles per line (4.5 %).
2. **The 1ppB lookup writes panel order and needs no index scaling.**
   `epd_build_1ppB_lut_1k()` now packs the same 256 (to,from) phases into four
   256-byte pages, page `p` pre-shifted to output bits `(2p+1):2p`, and
   `calc_epd_input_1ppB_pages()` ORs four byte loads (`lut_1ppb_pages.h`). The
   first pixel of a group is looked up in page 3, which is exactly the order
   `epd_output_reverse_pixels()` used to produce afterwards, so the feeder skips
   that pass too (`LutFunctionPair.output_panel_order`): −1 214 cycles per line
   (9.4 %). The S3 build keeps `lut.S`, the 256×4-byte table and the reversal.

Instruction counts from the built firmware (`.iram0.text`, `-O3`, one output
byte = four pixels): 32 → 24 for the plain lookups, 20 with the four-way unroll
in `epd_1ppb_convert()`. The estimate before flashing was ~7 000 cycles/line;
the device measured **8 340** (27.4 cycles per output byte), so the 37 %
instruction cut bought 25 % of cycles. The loop is load-bound, not
instruction-bound: it issues eight byte loads per output byte, four of them from
the PSRAM difference image, and a load costs ~3.4 cycles here against ~0.7 for
the ALU ops.

Measured on the device (2026-10-25, app 0.3.6, `build/device-boot-linepath-1.log`):

```
epd_feed: line path: lut=8340 mask=6 reverse=5 cycles/line over 683 lines (full_mask=1 panel_order_lut=1)
epd_feed: line path: lut=8320 mask=6 reverse=12 cycles/line over 682 lines (full_mask=1 panel_order_lut=1)
```

`mask`/`reverse` are the two `esp_cpu_get_cycle_count()` reads themselves: both
passes are gone. The line is 8 351 cycles against the 10 880 budget, **23 %
margin**, and **20 MHz now holds**: the boot frame that used to underrun
(0x400 → 10 MHz) finished in 695 ms, and a 15-minute paging session produced 85
refreshes and 78 taps with zero underruns, all at 20 MHz —

| Frame | At 10 MHz (before) | Now, 20 MHz |
| --- | --- | --- |
| Masked reading page turn | 966 ms | **531 ms** |
| Forward turn (stagger, 55 ticks × 16.3 ms) | ~1 272 ms | **941 ms** |
| Boot GC16 (clean) | underrun → 10 MHz | 695 ms |
| Small partial refresh | — | 36 ms |

The board no longer needs `clock_retry_step()` for this path; the fallback in
`board.c` stays as the safety net (one 3.4 s clear per boot at worst).

User policy (2026-11-20): the underrun fallback clock is **16 MHz**
(`THINGSOS_PANEL_SAFE_PCLK_MHZ` in `clock_recovery.inc`), not the BSP's 10 MHz
floor. The BSP solver gives line 21 µs (5+13+304+14 clk), frame ≈ 14.7 ms
(still above the 12 ms phase floor) and CKV high 19.8 µs with ≥1.2 µs low. The
same backoff then probes 20 MHz again. The BSP range (10–20 MHz) is unchanged.
Host tests cover it; device acceptance is still pending.

If the margin has to grow, the next step targets the load side: read the four
input pixels with one `lw` and three shift/mask pairs instead of four `lbu`
(8 → 5 loads per output byte). That keeps the same 20-instruction shape and the
same byte-exact host test.

Why the PIE unit is not used for this: the S31 does have XespV (`-march=…_xespv2p2`),
but a 256-entry lookup indexed by a data byte needs a gather, and the ISA has
exactly one candidate, `esp.ldxq.32`. The toolchain ships no C intrinsics for
XespV (only `riscv_vector.h` / `riscv_th_vector.h`), Espressif has not published
the instruction reference, and the only public P4/PIE reference — 229 tests
verified on hardware — records `ldxq` as "exact semantics unclear"
(<https://github.com/nullislandspace/tanmatsu-simd-tests/blob/main/PIE_REFERENCE.md>).
The feeder runs on both cores with lazily switched PIE state, so hand-written
gather assembly could not be validated here at all. The page layout reaches the
same budget with code `native/tests/lut_1ppb_test.c` checks byte for byte; a
gather path only makes sense after the instruction is pinned down on a device.

## Wi-Fi radio started on demand (2026-10-25)

The driver keeps its own NVS copy of the station config and, at radio start,
connects to it regardless of the app's profile list. With that AP out of range it
retried internally every ~22 ms for hours ("Haven't to connect to a suitable AP
now!"), pinning the high-priority Wi-Fi task on core 0 and flooding the console
(45 lines/s). Fixed by:

- clearing the driver's station copy at init (`esp_wifi_set_config(STA, empty)`);
- starting the radio only when needed (`wifi_radio_start()` from connect, scan,
  access point, or the transfer service) instead of at boot;
- a connect gate (one attempt per 2 s, stop after 4 failures) and a 6 s circuit
  breaker that stops the retry loop when no address arrives.

Verified: 30 s boot capture now contains zero "Haven't to connect" lines (was 1237)
and 101 total log lines (was 1300+), with the UI otherwise unchanged.

## What carried over unchanged

Metalio V1.5 uses an **E0470A14-AF** 4.7-inch, 34-pin, 8-bit panel; Read Pico
uses a 40-pin, 16-bit E0470 part. Both are 684×1216, and the panel supplier's
sample GC16/GL16/DU tables are byte-identical to the Read Pico tables, so the
same waveform is used. Those tables are **provisional** for this panel until the
supplier provides a factory LUT. The whole application layer is reused: library,
reader, EPUB/TXT/Markdown, fonts, statistics, WeRead, transfer web page, OTA,
rescue, boot guard, refresh policy, stagger turns and landscape reading. The
panel mapping is also the same (`EPD_ROT_INVERTED_PORTRAIT`).

Sources: GitHub CloudZao/Metalio-E-INK4-Plus `b400605` and the vendor debug
snapshot `Metalio-E-INK4-Plus-source-2.0.76-debug-20261007` (V1.5 handover,
`docs/display-debug.md`, `docs/tps65185-boot-overheat.md`).

## Panel-specific settings from the vendor 2.0.76 snapshot

| Setting | Value | Reason |
| --- | --- | --- |
| Line end | ≥10 XCL clocks after data (17 µs line at 20 MHz) | E0470A14-AF datasheet 6.3.1 |
| CKV | ≈15.8 µs high / 1.2 µs low at 20 MHz | covers active data; datasheet high/low ≥1 µs; vendor 15.9/1.1 µs confirmed monotonic 16 grays |
| Phase period | ≥12 ms | vendor tuning |
| Power-off | XOE low ≥12 µs before VPOS/VNEG off | datasheet 6.1 |
| Border hold | `METALIO_WHITE_BORDER_PIXELS` in `metalio_bsp.h`, **0 (off)**; set 8 to hold 8 physical edge pixels after the first clean frame | mirrors the vendor `DISPLAY_WHITE_BORDER_PIXELS`, which the vendor build now sets to 0; the 8-pixel hold was their fix for edges flashing black |
| Scan start | trigger waits (bounded) until every earlier prefill line is committed | vendor 2.0.66-debug race between the two feeder cores |
| Frontlight | 100 % = half PWM (duty 511 of 1023) | vendor 2.0.68-debug ceiling |
| VCOM | 1200 mV | vendor code setting only; no per-unit value in code, datasheet or panel label record |
| Waveform | vendor `MY_WAVEFORM` mode set: **full 48-phase GC16 and GL16** + the 20-phase threshold DU. Assembled at boot by `thingsos_board/panel_waveform.c` from the waves the reference component exports; the reference tables themselves are untouched | vendor 2.0.76 keeps the untrimmed tables on this panel (`MY_WAVEFORM_TRIM 0`); the trimmer's own README states trimming shows old-content ghosting first |

**Download-mode heat (V1.5 hardware):** the vendor reports the TPS65185 running
hot in BOOT/ROM download mode. On a cold power-up the TCA9555 pins default high
and can enable the panel rails while no application runs. Do not leave the
device in download mode; flash promptly. ThingsOS latches outputs low before
switching pin direction, never returns the pins to inputs, and switches the
rails off on a failed power-up.

## Display refresh (vendor-aligned)

Residual image after a page change on this panel comes from two places, both of
which the vendor firmware handles differently from the Read Pico tuning the port
inherited. `native/docs/HOME-PERFORMANCE.md` records that tuning; it was accepted
for the Read Pico E0470A01 sample panel and does not carry over.

| Item | Read Pico tuning (previously used here) | Metalio now (vendor 2.0.76) |
| --- | --- | --- |
| GC16 / GL16 tables | trimmed at boot to 36 / 37 phases | full 48 / 48 phases |
| DU | threshold table, 20 phases | unchanged |
| Page change | equal-pixel hold GC16 over the changed rows/columns | one all-pixel GC16 |
| Small in-page update | equal-pixel hold GC16 over the changed rows/columns | unchanged |
| Reading page turn | equal-pixel hold GC16 text view | unchanged |
| Periodic / explicit clean | full-pixel GC16 | unchanged |

**Waveform.** `e0470_waveform_trim()` shortens the boot tables by capping the
erase run (18 → 11 phases) and cutting the saturation head (5 phases). Its own
documentation states the trade: less erase margin, so ghosting of old content
appears first. The vendor build leaves the tables untrimmed on this panel.

**Drive path.** The official GC16 sequence drives equal pixels through their own
black-then-white cycle (for 15 → 15: 18 darken, then 17 white). That cycle is
what normalises the whole white background. Masking equal pixels to "hold"
removes it, so after a page change the pixels that changed are pushed to a clean
white while the untouched background keeps its previous state — which reads as
the inverse image of the page just left. The vendor conditions every screen
change with one all-pixel GC16 for exactly that reason, and so does this board
now. The equal-pixel hold view stays on for small in-page updates and reading
page turns, so a tap or a page turn still does not flash the whole page.

**Cost.** A page change now submits 48 phases instead of 36 at the calibrated
~12 ms phase period: about 590 ms of waveform submission instead of about
430 ms, and it flashes black once, like the vendor firmware.
`thingsos_board/panel_waveform.h` is the single place that defines the mode set;
if a mode is missing at boot the board falls back to the trimmed reference table
and logs the phase counts it resolved.

### Where a page change spends its time

One page change is one 48-phase GC16, so roughly 576 ms of it is waveform frames
(48 × 12 ms) and cannot be removed without changing what the panel is driven
with. The rest of the action is:

| Stage | Cost | Reducible |
| --- | --- | --- |
| UI layout (`draw()`) | ~60 ms | app side |
| canvas → 4bpp pack | ~56 ms | CPU only |
| diff + `back_fb` write-back | ~25–30 ms | no |
| 48 GC16 frames | ~576 ms | only by driving less |

Checked and rejected on this panel:

- **Leading-hold skip** (`epd_set_leading_skip`, enabled by the Read Pico BSP).
  It is lossless by construction, but it can only skip up to the earliest first
  action over the (to, from) pairs actually present. In the full GC16 table
  black → light gray (to 14, from 0) starts at phase 0, and any anti-aliased
  glyph edge produces that pair, so the skip is 0 for real pages while the
  histogram pass still costs a few ms. Left off.
- **Driving only changed rows/columns.** Measured on this controller in the
  earlier rounds: diff GC16 444–448 ms against full GC16 461–464 ms, about 4 %.
  Every frame walks all 684 gate lines, so the frame count dominates.
- **Shorter frame period.** The tables were authored for 11 090 µs frames and the
  board runs 12 000 µs, the vendor's floor ("short TB8 phases leave more DU/GL
  ghosting"). Matching the design point would save about 44 ms and spend exactly
  the margin that was just restored.
- **A shorter page table.** Any 38-phase variant has to cut the 18-phase blacken
  or the 17-phase white run, which is the refresh itself.

Removed instead:

- The boot path ran `epd_fullclear()` — physical clear plus an all-white
  48-phase GC16 — and then the page's own all-pixel GC16. The all-white frame is
  gone; the first page is drawn from white right after the physical clear,
  about 0.6 s less boot display time.
- A page change now restarts the periodic-clean counter, because it already
  re-drives every pixel. Before, it also counted as a partial and queued an
  identical full refresh every `refresh_every` changes.

Still available if single-action latency must drop further: move the panel update
to its own worker task with a pending frame (the vendor layout), so the next
action's layout and pack overlap the running 576 ms scan instead of serialising.
That does not shorten one page change; it removes about 110 ms per extra action
under rapid tapping and lets taps coalesce.


## What changed

| Area | Read Pico | Metalio |
| --- | --- | --- |
| Target | ESP32-S3, 16-bit bus | ESP32-S31 (RISC-V, 320 MHz), 8-bit bus at 20 MHz |
| Display driver | official epdiy fork | `native/metalio/components/epdiy` (S31 LCD/DMA, C instead of PIE SIMD, pixel order) |
| Panel power | MCU PMU + SY7636A, factory VCOM in PMU | TPS65185 via TCA9555; vendor VCOM 1200 mV written to volatile registers at each power-up, never EEPROM; VCOM_CTRL only during scans |
| Touch | CST836U | FT6336U; bezel HOME/PREV/NEXT (Y≈1600) mapped onto the existing key band |
| Keys | PMU events | POWER (TCA P1.7, long = 3 s), Vol+ previous page, Vol- next page |
| Battery | PMU | BQ27220 voltage-linear SOC + CX25601N charger (vendor policy, including 3.1 V BATFET cut-off) |
| Clock | PMU | PCF8563 RTC |
| Power-off | PMU command | TCA P1.3 pulse train |
| Sleep wake | 200 ms PMU poll | TCA INT (GPIO2) low level + 2 s timer; I2C lock held across sleep |
| SD | 1-bit, CD on expander | 4-bit SDMMC, no CD pin (presence = CMD13) |
| USB disk | FS PHY | HS UTMI PHY, ANA_SW (GPIO36) low while active |
| Flash layout | bootloader @0x0 | bootloader @0x2000; partitions unchanged |
| Online releases | board `RDP-G01-W` | board `metalio-e-ink4-plus`; S3 images refused by OTA and SD install |

New on Metalio: Settings → Display → **前光与触感** (frontlight off/warm/cool/both,
brightness 10–100 %, key vibration on/off), saved in NVS and restored at boot.

Code map: `native/metalio/components/metalio_bsp` (board drivers),
`native/firmware/components/thingsos_board/board.c` (ThingsOS board layer),
`native/metalio/components/epdiy/FORK-METALIO.md` (display fork changes).

## Build and package

```powershell
.\native\build.ps1 -Rescue     # build/native-rescue
.\native\build.ps1             # build/native-idf
python .\native\package_metalio.py   # build/native-usb/ThingsOS-metalio-0.3.6-usb.bin + manifest
```

`build.ps1` uses `D:\crossmux\.tools` unless `THINGSOS_TOOLS`, `IDF_PATH` or
`IDF_TOOLS_PATH` are set. Host tests: configure `native/` with CMake (MSVC) into
`build/native-host-cmake` and run `ctest`.

Flashing (only after a full flash backup, `esptool --chip esp32s31 --no-stub -p COMx read-flash 0 0x1000000 backup.bin`):
`esptool --chip esp32s31 --no-stub -p COMx write-flash 0x0 ThingsOS-metalio-0.3.6-usb.bin`.
The full image erases settings; SD content is untouched.

## On-device acceptance (in order)

1. Boot log: I2C scan shows 0x20, 0x38, 0x51, 0x55, 0x68, 0x6B, 0x19; TPS65185 rails up; first full clear.
2. Image: no 4-pixel-group scrambling (pixel order), no tearing, correct orientation, gray ladder (00–15 monotonic); check whether the edges flash black on full refresh (border hold is off; if they flash, set `METALIO_WHITE_BORDER_PIXELS` to 8); GC16/GL16/DU ghosting compared with the vendor firmware. Boot log must show `Panel waveform GC16=48 GL16=48 DU=20 phases`. Then switch 书库 → 传书 → 设置 repeatedly and confirm no residue of the previous page survives the change (that is the all-pixel GC16 path), and that a single tap inside a page still updates without a full-page black flash (equal-pixel hold path).
3. Underrun: no `EPD_DRAW_EMPTY_LINE_QUEUE` during page turns with Wi-Fi active; page-turn time from `ThingsOS_perf` logs.
4. Touch hit areas in portrait and landscape; bezel keys HOME (short menu, hold home), PREV, NEXT; volume keys.
5. POWER short → lock with wallpaper → wake; long 3 s → power-off on battery; light-sleep current.
6. Battery percentage and charging text on USB; charger log shows VREG 4400 mV / ICHG 1000 mA.
7. RTC keeps time across power-off; NTP updates it.
8. SD mount (4-bit), removal and remount; USB disk mode on a PC; return to normal after disabling.
9. Frontlight modes and brightness; off during lock, restored on wake; vibration.
10. Tilt page-turn axis direction: 2026-10-28 actual portrait wrist trace confirms Metalio uses chip Y; board mount rotation `(Y,-X,Z)` added. User confirmed portrait left/right turns and no turn on return after corrected firmware installation. See `METALIO-TILT-MOUNT.md`. Landscape and long-run physical acceptance remain pending; feature defaults off.
11. Wi-Fi, transfer page, WeRead sync, browser OTA, power-cut during OTA, rescue entry, ROM download.
12. Ported application screens on this panel: long-press selection plus save/edit/delete of an
    underline and thought; "My underlines and thoughts" for a local book and a cached WeRead
    book; Reader → More sync toggle (auto/manual) and "sync now"; 8-row file list, filter,
    open/delete/cancel; Pinyin word candidates (whole-word spelling with `'`), page crash-free
    with 64-character composition. Check that page changes still run the all-pixel GC16 and
    that a single in-page tap still avoids a full-page flash.

## Open items

- Web flasher site (`native/site`, `package_site.py`, `verify_build.py`) is still Read Pico/ESP32-S3 specific; use `package_metalio.py` and esptool for now.
- No Metalio release channel exists; the device refuses the Read Pico manifest by design.
- VCOM: Metalio has no per-unit calibration store; 1200 mV follows the vendor firmware. Confirm the panel's factory value with Xinzhi or the panel label.
- Waveform: provisional until the E0470A14-AF factory LUT is supplied. The mode set, phase counts and drive path now follow the vendor build; only the table contents themselves remain the supplier sample.
- Ghosting is still only a code-side change until it is observed on the device; the phase counts in the boot log are not optical evidence.
- Hardware revision: pins match V1.3–V1.5 (XSTL GPIO4, MODE GPIO19).
- Download-mode heat is a hardware issue (TCA9555 default-high pins); needs pull-downs or gating on the board.
- Bluetooth discovery, connection records and audio routing now have a native implementation; BH1098G mode-2 audio and headphone compatibility still require device acceptance. See [Bluetooth audio](BLUETOOTH-AUDIO.md). AI voice is not implemented.
