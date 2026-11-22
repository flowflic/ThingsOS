# Bounded offline chapter repair (2026-11-15)

The private book-25398009 cache contains 455 chapters. chapter-312.raw
(chapter 313, UID 767) has invalid UTF-8 at byte offset 8891 and a damaged
attribute near byte 8881. The old quote-aware normalizer consequently reaches
EOF inside a tag. Existing resume checks only identity and file length, so
repacking this source repeats normalize-tag-eof. This evidence does not locate
the original corruption in transport, decoding, or storage.

Offline generation now retries an identified source chapter for tag EOF,
invalid numeric entity, unclosed skipped markup, missing input, or a failed
per-chapter EPUB validation. It shares the normal download path: reader token,
shard requests, decode, temporary raw file, and atomic raw replacement. It uses
the saved TOC and chapter UID; healthy chapters are not downloaded again.
Both whole-book caching and local restore use this recovery loop. Local restore
can therefore need networking if a damaged source chapter is encountered.

Each chapter can be fetched once per operation, with at most three chapter
repairs across the book. A repeated failure or exhausted budget reports the
original pack/validation detail. Storage writes, memory, ZIP and chapter-size
failures are not automatically redownloaded. Cancellation and download errors
stop recovery. Existing committed EPUBs are replaced only after generation and
full chapter validation succeed. A whole-book ZIP response during repair is
rejected as a chapter replacement. Token prefetch workers stop before repair;
network channels close before each subsequent packing pass.

Regression cases in native/tests/weidu_test.c cover damaged saved markup,
failed shard retrieval retaining source and EPUB, successful single-chapter
restore, repeated corruption stopping after one fetch, and interrupted
whole-book recovery. Private book content is not committed as a fixture.

No version upgrade, flashing, publication, or on-device acceptance is included.

Validation: weidu_workflow passed (37.01 s); Metalio ESP32-S31 main firmware
build passed at the existing 0.1.9 version, including image-size and JPEG
symbol checks. Logs: build/weidu-book-25398009-20261115/repair-host-test.log
and build/weidu-repair-firmware-20261115.log. Actual account redownload and
on-device recovery remain unverified.
