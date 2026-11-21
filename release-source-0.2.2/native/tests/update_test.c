// SPDX-License-Identifier: Apache-2.0
#include "thingsos/update.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    size_t read, written;
    int failure;
    bool began, aborted, ended, activated;
} fake_t;
static int receive(void* p, uint8_t* b, size_t n) {
    fake_t* f = p;
    if (f->failure == 1 && f->read > 0) return 0;
    if (n > 37) n = 37;
    memset(b, 7, n);
    f->read += n;
    return (int)n;
}
static bool header(void* p, const uint8_t* b, size_t n) {
    return ((fake_t*)p)->failure != 2 && n == 32 && b[0] == 7;
}
static bool hash_begin(void* p) {
    return ((fake_t*)p)->failure != 3;
}
static bool hash_update(void* p, const uint8_t* b, size_t n) {
    (void)b;
    (void)n;
    return ((fake_t*)p)->failure != 4;
}
static bool hash_finish(void* p, uint8_t b[32]) {
    memset(b, ((fake_t*)p)->failure == 5 ? 8 : 7, 32);
    return ((fake_t*)p)->failure != 6;
}
static void hash_abort(void* p) {
    (void)p;
}
static bool begin(void* p, size_t n) {
    (void)n;
    fake_t* f = p;
    return f->began = f->failure != 7;
}
static bool write_chunk(void* p, const uint8_t* b, size_t n) {
    (void)b;
    fake_t* f = p;
    f->written += n;
    return f->failure != 8;
}
static bool end(void* p) {
    fake_t* f = p;
    f->ended = true;
    return f->failure != 9;
}
static void abort_update(void* p) {
    ((fake_t*)p)->aborted = true;
}
static bool activate(void* p) {
    fake_t* f = p;
    if (f->failure == 10) return false;
    f->activated = true;
    return true;
}
int main(void) {
    thingsos_update_request_t r = { .bytes = 9000,
                                    .header_bytes = 32,
                                    .capacity = 4 * 1024 * 1024,
                                    .running_address = 0x220000,
                                    .target_address = 0x620000,
                                    .target_is_ota = true };
    memset(r.expected_sha256, 7, 32);
    for (int failure = 0; failure <= 10; failure++) {
        fake_t f = { .failure = failure };
        thingsos_update_ops_t o = { &f,          receive,     header,       hash_begin,
                                    hash_update, hash_finish, hash_abort,   begin,
                                    write_chunk, end,         abort_update, activate };
        bool ok = thingsos_update_run(&r, &o);
        assert(ok == (failure == 0));
        assert(f.activated == (failure == 0));
        if (f.began && !f.ended) assert(f.aborted);
        if (failure == 0) assert(f.written == r.bytes && f.ended);
    }
    fake_t f = { 0 };
    thingsos_update_ops_t o = { &f,          receive,     header,       hash_begin,
                                hash_update, hash_finish, hash_abort,   begin,
                                write_chunk, end,         abort_update, activate };
    r.target_address = r.running_address;
    assert(!thingsos_update_run(&r, &o) && !f.began && !f.read);
    r.target_address = 0x20000;
    r.target_is_ota = false;
    assert(!thingsos_update_run(&r, &o) && !f.began);
    r.target_is_ota = true;
    r.bytes = r.capacity + 1;
    assert(!thingsos_update_run(&r, &o) && !f.began);
    r.bytes = 31;
    assert(!thingsos_update_run(&r, &o) && !f.began);
    puts(
        "OTA transaction: truncation, header, hash, begin/write/end/activate failures and slot "
        "protection passed");
    return 0;
}
