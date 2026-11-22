/* SPDX-License-Identifier: Apache-2.0 */
/* Parked playback resume point: the next undecoded byte of the audio data. */
#include "thingsos/audio_format.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            fprintf(stderr, "Audio resume test line %d: %s\n", __LINE__, #x); \
            exit(1);                                                     \
        }                                                                \
    } while (0)

int main(void) {
    /* 10 MB of MP3 data, 6 MB still unread, 300 KB buffered but not decoded:
       decoding consumed exactly 4 MB - 300 KB. */
    CHECK(thingsos_audio_resume_byte(10000000u, 6000000u, 300000u) == 3700000u);
    /* Nothing read yet, or everything read and decoded. */
    CHECK(thingsos_audio_resume_byte(10000000u, 10000000u, 0) == 0);
    CHECK(thingsos_audio_resume_byte(10000000u, 0, 0) == 10000000u);
    /* End of file with the tail still buffered. */
    CHECK(thingsos_audio_resume_byte(10000000u, 0, 4096u) == 10000000u - 4096u);
    /* Inconsistent bookkeeping clamps to the start instead of wrapping. */
    CHECK(thingsos_audio_resume_byte(1000u, 2000u, 0) == 0);
    CHECK(thingsos_audio_resume_byte(1000u, 500u, 600u) == 0);
    /* Files beyond 4 GiB keep 64-bit offsets. */
    CHECK(thingsos_audio_resume_byte(6000000000ull, 1000000000ull, 512u * 1024u) ==
          5000000000ull - 512u * 1024u);
    puts("audio resume tests passed");
    return 0;
}
