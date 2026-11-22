# Audio task startup memory correction (2026-11-14)

The reported Chinese message is emitted only when creation of `local_audio`
fails, before file parsing or audio buffer allocation. The previous worker
required a contiguous 32768-byte internal SRAM stack. Device heap logs for the
reported failure have not been captured, so total exhaustion versus fragmentation
is not distinguished.

The worker now requests 16384 bytes of internal SRAM. MP3 scratch is allocated
in PSRAM with the decoder buffers, checked before decoding and freed on every
finish/error/seek. Internal task-stack placement is retained for NVS writes.
Allocation failure logs both internal free bytes and the largest free block.

`firmware/components/thingsos_audio/minimp3_psram.h` is a CC0 derivative of the
pinned `vendor/minimp3/minimp3.h` (SHA-256: 57e437c5c1f0e8b243885d3929c8973b5e6c778451e0100ab4251d19915cb3ad).
Its original notice remains intact. Only the frame decoder's scratch declaration
and its references are adapted to use caller-owned storage. The pinned vendor
copy and reference checkouts are unchanged. The adaptation is single-worker only.

Validation:
- Main ESP32-S31 firmware build and application JPEG linkage checks passed.
- Compiler stack report: worker 1216 bytes, start 720 bytes, decode_frame 80 bytes
  (individual frames, not whole call-chain bounds or runtime high-water marks).
- Original/adapted decoders matched sample count, frame metadata and PCM over
  4000 deterministic synthetic/random input cases. This is not an audio listening
  test. Harness and reports live in `build/audio-memory-20261114/`.
- Existing host recording-denoise boundary tests passed.

Version remains 0.1.8. No device flash, publication or commit performed.
Device acceptance remains pending: MP3/WAV startup with Wi-Fi and reader caches,
track changes, seeking, stop/restart, volume persistence, recording pause/resume,
and runtime minimum internal heap/stack high-water marks, including NSNet3.
