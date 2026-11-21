# Metalio lock/wake full ripple (2026-11-12)

The user reports retained lock-wallpaper ghosts after unlocking into reading.
Both suspend and unlock now use the same complete GC16 ripple and immediate
panel rail-off path. The existing version remains 0.1.7; nothing is flashed or
published by this change.

## Source findings and change

The UI already sets `lock_wave_pending` on wake, including when it restores the
reader directly rather than navigating away from `THINGSOS_UI_LOCK`. However,
`lock_wave_turn()` selected `stagger_turn(..., clean=false)`: the reader's
low-flash view suppresses most equal-pixel actions, including all but one of
the equal-white maintenance actions. Full-screen scan delivery alone therefore
did not mean full GC16 conditioning. On allocation failure, wake also used a
plain full update while suspend used a physical clear followed by repaint.
These are source findings, not a measured optical diagnosis of the device.

- `lock_wave_turn()` now selects the existing `clean=true` stagger variant.
  All 48 phases of the original board waveform reach every pixel, including
  unchanged white/gray; eight bands use the existing two-tick cleanup gap
  (62 scheduled ticks). Lock and wake retain opposite directions.
- Suspend calls `thingsos_board_present_lock_transition(..., true)`, and wake
  uses the same function with `false`. Both set the full-refresh request and
  use the retained-frame cleanup flag, disabling the normal three-second UI
  rail reuse window. They return after immediate panel shutdown/idle checks.
- Successful lock/wake ripple counts as a complete clean and resets the periodic
  refresh budget. It remains independent of reader ripple and refresh settings.
- Unavailable ripple, including insufficient SRAM, falls back identically in
  either direction to `epd_clear_checked()` followed by GC16 from white. Scan
  failures retain the established safe-clock recovery and do not commit a
  partial frame as successful. Failed wake presentation retains its UI request.
- Pending lock/wake output takes priority over modal-region and reader output.
  Ordinary reader low-flash turns and their waveform are unchanged.

The patch selects existing waveform/cadence variants. It does not modify any
upstream waveform bytes, panel timing constants, factory VCOM or voltage policy.

## Validation

`stagger_waveform_failure_paths` checks both directions, 48 phases/62 ticks,
every row's complete source LUT actions, multiple equal-white maintenance actions,
unchanged software-white frames, OOM fallback, scan failure, safe-clock recovery
and successful-frame history commits. `device_ui_controller` checks direct wake
into reading retains position, requests reverse lock output and keeps that
request after a failed presentation until retry succeeds.

Main/rescue builds and host regression logs, the pre-change source snapshots,
candidate artifacts and hashes are recorded under
`build/unlock-full-ripple-20261112/`. Physical wallpaper-to-reader residuals,
animation appearance and actual refresh latency remain pending device acceptance.
