"""Move only the pinned trim scratch array to a temporary workspace.

The reference checkout and waveform algorithm remain unchanged. Generated
sources retain upstream SPDX notices. Main uses PSRAM only; a rescue without
PSRAM may borrow internal memory during initialization and releases it again.
"""
from pathlib import Path
import argparse, hashlib, json

EXPECTED = '83e1a65aa6f1be20ffe127b258ec3dcc72c01ae42bf29ee5aed9a1de2276016c'

def generate(source, output):
    source, output = Path(source), Path(output)
    raw = source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == EXPECTED, 'Pinned waveform trim source changed'
    text = raw.decode('utf-8').replace('\r\n', '\n')
    def replace(old, new):
        nonlocal text
        assert text.count(old) == 1, old
        text = text.replace(old, new)
    replace('#include <string.h>', '''#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_psram.h"
static void* thingsos_trim_alloc(size_t bytes) {
    if (esp_psram_is_initialized())
        return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#ifdef THINGSOS_TRIM_RESCUE
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return NULL;
#endif
}
#define thingsos_trim_free heap_caps_free
#else
#include <stdlib.h>
#define thingsos_trim_alloc malloc
#define thingsos_trim_free free
#endif''')
    replace('    static trimmed_seq_t seqs[16][16];',
            '    trimmed_seq_t (*seqs)[16] = thingsos_trim_alloc(sizeof(trimmed_seq_t) * 16 * 16);\n'
            '    if (seqs == NULL) return 0;')
    replace('    if (phases > src->phases) return 0;',
            '    if (phases > src->phases) { thingsos_trim_free(seqs); return 0; }')
    replace('    return phases;', '    thingsos_trim_free(seqs);\n    return phases;')
    output.mkdir(parents=True, exist_ok=True)
    target = output / source.name
    data = text.encode('utf-8')
    if not target.exists() or target.read_bytes() != data: target.write_bytes(data)
    meta = dict(upstream_sha256=EXPECTED, generated_sha256=hashlib.sha256(data).hexdigest(),
                workspace_bytes=17408, persistent_internal_workspace=False,
                main_internal_fallback=False, rescue_without_psram_temporary_internal=True)
    (output/'patch.json').write_text(json.dumps(meta, indent=2)+'\n', encoding='utf-8')
    return meta

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    generate(args.source, args.output)
