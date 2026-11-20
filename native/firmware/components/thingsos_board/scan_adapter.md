# LCD scan adapter provenance

`scan_adapter.c` derives from the pinned official MindReset Read Pico firmware:

- Repository: https://github.com/MindReset/read_pico_firmware
- Commit: `ed1e9331077451a0391769cd6c191568f7901807`
- File: `components/epdiy/src/output_lcd/render_lcd.c`
- License: LGPL-3.0-or-later, retained under `ThingsOS/licenses/LGPL-3.0.txt`
  and the official component's `LICENSE` / `idf_component.yml`.

The local copy adjusts include paths and adds a synchronous row-table selector.
Only the LUT supplied to the existing vector lookup varies by physical row.
Queues, DMA lifecycle, line masks, pixel clock, voltages and scan timing are
unchanged. `native/scan_adapter.cmake` replaces exactly the original LCD renderer
translation unit in both firmware builds; it never edits the reference checkout.

`stagger.inc` and `thingsos/stagger.h` are independently authored scheduling code.
They compute one historical difference image. Ordinary text turns preserve every
changed-pixel GC16 action, keep stable grays still, and retain one source white
push for stable white. All physical rows and columns receive this view because
a software "already white" pixel may still carry ink. Eight overlapping strips
start one scan apart (36 phases, 43 ticks). Reader full refresh is DIRECT GLOBAL
GC16, per the user's 2026-10-17 visual feedback, and never enters this animation.
Only a successful final scan commits the target as historical state. The caller
rebuilds from white after any scan failure; allocation failure falls back to
full-pixel output. Startup and error recovery retain conventional global GC16.

The current-band LUT allocation is 8 KiB internal SRAM, with a 16 KiB heap reserve,
released after each turn. Each tick builds only its current eight phase tables;
deadline-bound feeder lookups do not access random PSRAM tables. There
is no previous-page 8bpp allocation and no intermediate DU presentation. FULL
scan timing and 11,090 us minimum tick pacing are retained; actual wall time
includes preparation, scan overhead and contention with prefetch. The schedule
floor is 477 ms (43 ticks), not a device measurement. All source trailing holds
remain; LCD prefill is 127. The main task joins font pre-rendering before the
first scan, including non-animated output, so PSRAM/font work does not compete
with the DMA feeders. Source tests and real thread barrier tests are separate
from physical panel acceptance.

No modified kiikoread driver or business code is included.
