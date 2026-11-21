# LCD scan adapter provenance

`scan_adapter.c` derives from the pinned official MindReset Read Pico firmware:

- Repository: https://github.com/MindReset/read_pico_firmware
- Commit: `ed1e9331077451a0391769cd6c191568f7901807`
- File: `components/epdiy/src/output_lcd/render_lcd.c`
- License: LGPL-3.0-or-later, retained under `ThingsOS/licenses/LGPL-3.0.txt`
  and the official component's `LICENSE` / `idf_component.yml`.

The local copy adjusts include paths and adds a synchronous row-table selector.
Only the LUT supplied to the existing vector lookup varies by physical row.
Queue storage, DMA lifecycle, line masks, pixel clock, voltages and scan timing are
unchanged. The original two feeder tasks remain. The requested prefill is
bounded by both ring capacities plus the two in-flight producer rows, including
leading blank rows, so cropped scans cannot fill both queues before the start
condition. No extra buffer or task stack is allocated. The original underrun
clear-and-redraw remains intact.
The adapter also removes the inherited, unpaired ROM
`Cache_Start_DCache_Preload` call. ESP-IDF's ROM header says it suspends automatic
preloading and requires an End call to restore it; application SDK code must
not own this shared engine. Rows now use ordinary cached loads, without changing
SDK cache arbitration. Fault phase/row/producer counters are logged after the
DMA and producers stop, never from the interrupt callback.
`native/scan_adapter.cmake` replaces exactly the original LCD renderer
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

## 2026-11-15 portrait reader edge-clean candidate

**Superseded after device test:** the user rejected the flashing left band.
The complete-GC16 band selection has been removed. The implementation below
describes the withdrawn first candidate only.

The user reports prior-page strokes only beside the first text column, cleared
by manual full refresh. The canvas clears/restores complete body rows, while
the ordinary text waveform suppresses equal-gray actions and retains only one
equal-white pulse. This is a plausible optical cleanup deficit, not a measured
scan fault or a physically confirmed root cause.

Balanced portrait page turns now use the official full GC16 actions in physical
band 7 (rows 599..683, logical x=0..84); other bands keep the existing text view.
The location is independent of forward/backward animation. With animation off,
all bands start together (gap zero, 36 phases); animation retains its 43 ticks.
There is no extra presentation or allocation. The edge band can visibly flash
more strongly. Menus, background annotation updates, landscape, fast mode,
lock/wake, and periodic full cleanup retain their existing policies.

Host tests verify the official-versus-text table selection for every physical
row and every phase in both directions, with and without animation. Existing
allocation fallback and failed-scan history tests remain. Actual residue removal
and local flashing require the same-page device comparison; no physical
acceptance is claimed.

### Revised candidate: extend the erased-stroke footprint left by 8 pixels

The page-turn difference already covers the full physical screen. Expanding a
rectangle cannot restore missing coverage. Instead `reader_erase.h` extends only
the first column's actual old-ink-to-white strokes towards logical left, by up
to 8 pixels. In the rotated framebuffer that means increasing physical row.
Only destinations white in both real frames qualify; current text and all other
transitions remain intact. Sources are restricted to physical rows 599..683.

The expanded fringe receives the same source-gray-to-white waveform as its
neighboring erased stroke, in the temporary difference image only. This is an
intentional cleanup footprint, not a claim that the real previous framebuffer
contained those fringe pixels. Both historical and target framebuffers remain
unchanged. Source reads always use those real buffers, preventing recursive
expansion beyond 8 pixels. The original text LUT now applies to every band;
there are no complete equal-pixel GC16 actions in the left band. No additional
scan, phase, frame buffer or allocation is introduced. Animated and synchronous
balanced portrait turns share this operation. Ordinary stroke-level flashing
and residue removal still require physical evaluation.

Regression tests cover rotation direction, both nibble parities, exact 8-pixel
limit, panel-edge clipping, new-text protection, untouched other columns and
unchanged every-band text LUTs in both turn directions and animation settings.

### Rollback after second device test (2026-11-15)

The user also rejected flashing from the 8-pixel erased-stroke expansion.
Both experimental cleanup candidates are withdrawn. The expansion helper,
its test-only cases, full-wave edge selection, and synchronous band route have
been removed. `board.c` again enters the band scheduler only for the original
wave path; non-animated text uses the original high-level text waveform.
The ordinary stagger signature, band gap, and phase guard are restored.
No root cause or residue fix is claimed by this rollback.
