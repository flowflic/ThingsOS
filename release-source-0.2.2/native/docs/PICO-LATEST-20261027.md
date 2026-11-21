# Pico reader updates on Metalio, 2026-10-27

The source was `D:\crossmux\ThingsOS`: committed HEAD `a509301` (0.3.8),
plus its uncommitted 0.3.9 candidate as inspected on 2026-10-27. That candidate
is not a released version. The Metalio firmware version remains **0.3.6**.
The normalized source snapshots used for the three-way merge are under
`build/pico-latest-20261027/merge/`; `reader_jump_job.inc` was copied from the
same worktree. The Pico checkout was not modified.
For traceability, the source `main.c` normalized to LF has SHA-256
`442ee04a0905febda4f695b4a9dfbbff62a5bef1ae6a5fab031826e3c6b81230`;
the copied `reader_jump_job.inc` has SHA-256
`5cce7744a8efa76be1c6a18ed557110d501e1d23840c31b12dd8df6d5771dd17`.

## Shared changes

- Full-book page indexing is row/time sliced and cancellable. A percentage
  jump waits for the complete index rather than treating a partial page count
  as the whole book. Failures, cancellation, timeout and stale book/layout
  state keep the existing reading position.
- Input is serviced before optional pagination/cache work; reader controls,
  chapter/bookmark pages and fixed reader-font buttons use the newer buffered
  touch handling. Font selection entered from reading returns to the reader.
- Reader menus, chapters, bookmarks, jump, fonts and more use a reader-surface
  refresh path. Automatic/setting-triggered cleanup is deferred across those
  controls and the first return frame; explicit, first-frame and recovery
  clears remain available.

The Metalio-specific input, control centre, Bluetooth, recording/audio and
frontlight paths remain in place. The reader-surface path uses this board's
existing full-pixel white-maintenance reader waveform, not a replacement Pico
driver. Panel waveform tables, scan timing, VCOM and charger policy were not
changed. The effect on flicker and ghosting still needs observation on this
panel; the Pico candidate did not have completed optical acceptance either.

Pico version/release-site changes and S3-specific USB/board implementation
were not carried over. The two projects intentionally retain their own board,
OTA identity and packaging.

## Local OTA investigation

The user repeated the 0.3.6 beta OTA. The device's read-only `/api/status`
reported `ota_0`, `rescue=false`, `healthy=true`, and ELF SHA-256
`dc39faca6aa9b65eae98e97340eaa2da17d0ba4fbd97a74c367984ab6bc61a38`,
matching `build/ota-beta-20261027/manifest.json`. Thus that reproduction
started the requested beta successfully despite returning to the library;
its internal version intentionally remains 0.3.6. The serial monitor did not
capture that installation, so this observation does not establish why an
earlier attempt appeared to fail. In that beta, `healthy=true` could also
come from OTA metadata; it is not independent evidence of the current boot's
30-second health log.

The transfer page now distinguishes upload, SD verification, rescue install
and startup-health confirmation, describes the single-slot recovery boundary,
and compares the running ELF instead of relying on a version string. It retries
the read-only health check after temporary network loss and does not claim a
reinstall when the pre-update ELF was the same or unknown. Normal and rescue
web flows describe their different install routes. The local OTA path also
logs failure stages and reports if the restart task cannot be created. The
new status response and OTA gate use the current boot's startup-health
confirmation rather than accepting an older valid OTA metadata state.

The upgraded website is embedded in the candidate firmware, so it becomes
available only after that candidate itself is installed. Returning to the
library remains the normal boot flow; the browser asks the user to reopen the
transfer page so it can check the running image.

## Validation boundary

Host regression passed **32/32**. Separate ESP-IDF v6.1 builds succeeded for
the main image (6,469,760 bytes / 11,403,264-byte main slot) and rescue image
(1,343,936 bytes / 2,097,152-byte rescue partition). The common IDF size
checker also compares the main image to the rescue partition and warns about
that irrelevant comparison; the images were checked against their actual
slots separately. Both images passed esptool checksum and validation-hash
checks. The candidate ZIP and its contents were read back and verified.
Logs, manifest and the local OTA candidate are in `build/pico-latest-20261027/`.

The newly merged candidate has not been flashed, published, or tested on the
Metalio display. OTA power-loss recovery, actual touch/reader behavior and
panel flicker/ghosting remain device acceptance items. The successful beta
observation above refers to the earlier beta image, not the newly built
candidate.
