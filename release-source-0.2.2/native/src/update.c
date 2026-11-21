// SPDX-License-Identifier: Apache-2.0
#include "thingsos/update.h"
#include <string.h>
bool thingsos_update_run(const thingsos_update_request_t* r, const thingsos_update_ops_t* o) {
    if (!r || !o || !o->read || !o->header || !o->hash_begin || !o->hash_update ||
        !o->hash_finish || !o->hash_abort || !o->begin || !o->write || !o->end || !o->abort ||
        !o->activate || !r->target_is_ota || r->running_address == r->target_address ||
        !r->header_bytes || r->header_bytes > 4096 || r->bytes < r->header_bytes ||
        r->bytes > r->capacity)
        return false;
    uint8_t chunk[4096], digest[32];
    size_t got = 0;
    void* context = o->context;
    while (got < r->header_bytes) {
        size_t wanted = r->header_bytes - got;
        int n = o->read(context, chunk + got, wanted);
        if (n <= 0 || (size_t)n > wanted) return false;
        got += (size_t)n;
    }
    if (!o->header(context, chunk, got)) return false;
    bool started = false, ok = o->hash_begin(context);
    if (ok) ok = started = o->begin(context, r->bytes);
    if (ok) ok = o->hash_update(context, chunk, got) && o->write(context, chunk, got);
    size_t remaining = r->bytes - got;
    while (ok && remaining) {
        size_t wanted = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
        int n = o->read(context, chunk, wanted);
        if (n <= 0 || (size_t)n > wanted) {
            ok = false;
            break;
        }
        ok = o->hash_update(context, chunk, (size_t)n) && o->write(context, chunk, (size_t)n);
        remaining -= (size_t)n;
    }
    if (ok) ok = o->hash_finish(context, digest);
    o->hash_abort(context);
    unsigned difference = 0;
    if (ok)
        for (size_t i = 0; i < sizeof(digest); i++) difference |= digest[i] ^ r->expected_sha256[i];
    if (difference) ok = false;
    if (ok) {
        ok = o->end(context);
        started = false;
    }
    if (started) o->abort(context);
    return ok && o->activate(context);
}
