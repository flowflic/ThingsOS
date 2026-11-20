# Reader vertical body flow (2026-11-12)

In **阅读设置 → 更多 → 竖向排版**, enable traditional vertical body text:
characters run top-to-bottom and columns run right-to-left. The switch defaults
to **关闭** when the `thingsos-ui/vertical` NVS key is absent or invalid. Existing
devices keep horizontal text until explicitly enabled. The setting is global,
persistent and independent of landscape rotation, immersive mode and refresh.
Version 0.1.7, flash partitions and prior audio/lock fixes are preserved.

## Implementation

The independent `graphics.c` text engine and `vertical_text.inc` use the same
UTF-8, paragraph, indentation, closing-punctuation, Markdown, underline, footnote
and image parser for either flow. A transposed **internal** canvas changes line
axes without rotating the text bitmaps or the final panel canvas. No second
full-screen framebuffer is allocated. Chinese glyphs, Latin letters and digits
remain upright, one character per cell. This does not implement Latin word
rotation, ruby or number combination within a cell.

Punctuation uses the active font's Unicode vertical presentation glyph when
available. Brackets, dashes and ellipses rotate their existing raster if such a
glyph is unavailable; commas and full stops are positioned at the upper-right
of the cell. Horizontal glyph metrics/rasters stay reusable in their original
cache, and the temporary flow state is restored before UI or note-string layout.

Pagination, foreground drawing and adjacent-page preparation all call the same
explicit flow API. Existing horizontal APIs retain their behavior, including
note cards and settings. Interruptible cursors retain layout coordinates;
touch boxes are converted to physical screen coordinates at every return and
converted back before a resumed slice. Footnote taps, annotation selection and
saved underline ranges therefore retain their original source-byte identity.
Vertical selection indicators follow the side of the text cell.

Images remain upright and retain their aspect ratio. Layout swaps image extents
on the flow axes, then passes physical bounds and the original canvas to the
existing decoder/hit-region callback. Image hints retain horizontal labels.
The existing full-page comic sizing rule is retained.

Changing the setting preserves the current source byte offset. On successful
NVS commit it invalidates pagination, history, pending cursors, text hits and
pre-rendered pages. A failed save restores the previous flag. Vertical layout
adds a distinct page-cache salt; disabling it restores the prior horizontal
key, so existing horizontal indexes are not unnecessarily invalidated.

The More page keeps its existing fonts and segmented controls. Portrait rows
are spaced 100 px apart and its two footer buttons share a row; landscape adds
the switch in the existing second-column space. All controls, including cloud
note synchronization, remain within the viewport.

## Verification

- `reader_vertical_layout`: real-font raster and measurement agreement;
  top-to-bottom/right-to-left source order; UTF-8 continuity across pages;
  portrait/landscape and immersive bounds; guarded buffers; pixel/hit equivalence
  after interrupt/resume; underline/footnote geometry; upright image aspect ratio;
  punctuation fallback and unchanged horizontal pixels when disabled.
- `device_ui_controller` (`vertical_ui_test.inc`): missing/invalid keys default
  off, failed-save rollback, persistence, independent landscape setting, source
  position preservation, distinct/restored index keys, physical text selection
  and pre-render cache recovery. Settings and reading snapshots are produced by
  the real UI renderer.
- Host regression, main/rescue build logs, previews and artifact hashes are under
  `build/vertical-text-20261112/`. Host screenshots verify layout, not panel optics
  or physical input acceptance. No device flashing or publication is performed.

This change is implemented in the Metalio tree. No legacy application code,
reference checkout or pinned third-party source is modified.
