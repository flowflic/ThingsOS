# Metalio single-microphone recording denoise (2026-11-12)

New recordings pass the existing left I2S microphone channel through **NSNet3**
before saving mono PCM16 WAV at 16 kHz. Processing is local and enabled for all
new recordings, including recordings later uploaded to IMA. It does not process
existing files or playback. The right I2S slot is not treated as another mic.
The firmware version and all partition offsets/sizes are preserved.

## Sources and integration

- Espressif ESP-SR 2.6.2, commit
  `bb552f1a559396a17681299cb3e199a0f1a4520a`: the unmodified ESP32-S31
  `libnsnet3.a`, the interface/model catalogue headers and NSNet3 index/weights.
  ESPRESSIF MIT permits use on Espressif products; retained interface headers
  also carry their original Apache-2.0 notices.
- Espressif dl_fft 0.8.0, esp-dl commit
  `512ed0b0dc5f5320f3d9ca5a5aea189803f87f31`: unmodified MIT sources and
  target selection. The upstream directory named `esp32p4` is also the official
  FFT assembly selection for ESP32-S31; this is not a P4 inference archive.
- `vendor/sources.json` pins every selected file by SHA-256; the existing
  `prepare_dependencies.py` verifies them offline before builds. All upstream
  sources and licenses stay intact, and reference checkouts are unchanged.
- `firmware/components/thingsos_audio/record_denoise.c` independently supplies
  the in-memory catalogue ABI declared in the pinned `model_path.h`. The two
  immutable model files are embedded by IDF in the main image and checked for
  expected sizes before creating an instance. No model partition, SD download,
  network request or first-run setup is required.
- The adapter binds the pinned archive's exported `esp_nsnet3` interface directly.
  The general ESP-SR selector also references NSNet2/4 and would pull unrelated
  networks and inference kernels. Updating this archive requires an ABI review.

## Recording behavior

The `local_audio` worker owns inference, its buffers and the model lifecycle.
The portable `src/audio_denoise.c` framer collects arbitrary read sizes into
256-sample hops, removes the one-hop STFT startup delay and drains overlap with
zero padding at pause/stop. Only real input samples are retained, so stopping a
very short recording does not drop its last word or add padding to its duration.
There is no VAD-based silence removal or automatic gain increase.

Pause closes the microphone, drains/saves the segment and frees the network.
Resume creates a fresh network before reopening the microphone; paused audio
and recurrent/overlap state are not included in the next segment. Stop and error
paths release the network and preserve successfully processed samples. A failed
model initialization or inference is reported explicitly; there is no silent
fallback to unprocessed recording. Playback does not allocate the network.

Existing I2S bit conversion (`>>12` and PCM16 saturation) is retained because
the external BH1098G's effective bit alignment still needs physical validation.
Logs report input/clipped samples and preclip peaks, plus per-segment input/output
counts, mean/max inference time and failures. This separates upstream clipping
from algorithm and scheduling problems during device acceptance. `status.peak`
uses the processed audio; elapsed recording time counts real captured samples.

## Validation and acceptance boundary

Host test `recording_denoise_stream_boundaries` covers 0/1/255/256/257 and longer
recordings, varying chunk sizes, delay removal, exact tail/duration preservation,
independent pause segments, bounded output and backend failures. Its deterministic
delayed backend checks stream mechanics; it does **not** run the RISC-V neural
network or establish acoustic quality. ESP-IDF build/link verifies target code,
the embedded weights and fit in the existing main partition.

Current build/test evidence is under `build/mic-denoise-20261112/`. No device has
been flashed for this change. Physical acceptance remains:

1. Quiet speech, fan/air-conditioner noise, music/TV and nearby competing voices,
   comparing the same speaker, distance, orientation and background volume.
2. Short recordings, repeated pause/resume/stop and WAV duration/tail checks.
3. Recording while paging/refreshing, with inference timing, I2S continuity,
   clipping and free-memory logs; repeated recording/playback lifecycle checks.
4. Confirm voice clarity and acceptable speech distortion. Single-mic denoise
   is not guaranteed to isolate the desired speaker from another human voice.

Sources: [NSNet documentation](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s31/nsnet/README.html),
[pinned ESP-SR](https://github.com/espressif/esp-sr/tree/bb552f1a559396a17681299cb3e199a0f1a4520a),
[pinned FFT](https://github.com/espressif/esp-dl/tree/512ed0b0dc5f5320f3d9ca5a5aea189803f87f31/tools/dl_fft).
