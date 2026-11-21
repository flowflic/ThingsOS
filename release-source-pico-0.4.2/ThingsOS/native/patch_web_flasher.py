"""Patch a pinned vendor reset entry point with the independently written S3 adapter."""
import hashlib
import shutil
from pathlib import Path

ORIGINAL_SHA256 = '6dcfc30fb4bbf18e19a141c5eb9a694edafc5d4480b45762c221173f47effdb5'
ANCHOR = 'async after(e="hard_reset",t,i){switch(e){case"hard_reset":'
REPLACEMENT = ANCHOR + 'if(this.chip?.CHIP_NAME==="ESP32-S3"){await thingsosReset(this);break;}'
FINISH = 'wa=async(e,t)=>{await e.setRTS(!0),await xe(100),await t.after()}'
FINISH_REPLACEMENT = 'wa=async(e,t)=>{if(t.chip?.CHIP_NAME!=="ESP32-S3"){await e.setRTS(!0);await xe(100)}await t.after()}'


def patch(folder):
    target = Path(folder) / 'install-dialog-im156JnI.js'
    original = target.read_bytes()
    assert hashlib.sha256(original).hexdigest() == ORIGINAL_SHA256, 'Unexpected vendor bundle; review reset adapter before publishing'
    text = original.decode('utf-8')
    assert text.count(ANCHOR) == 1
    assert text.count(FINISH) == 1
    target.write_text('import {resetS3AfterInstall as thingsosReset} from "./thingsos-s3-reset.js";\n' + text.replace(ANCHOR, REPLACEMENT).replace(FINISH, FINISH_REPLACEMENT), encoding='utf-8')
    shutil.copyfile(Path(__file__).parent / 'site/thingsos-s3-reset.js', Path(folder) / 'thingsos-s3-reset.js')
    return dict(original_sha256=ORIGINAL_SHA256, patched_sha256=hashlib.sha256(target.read_bytes()).hexdigest(), adapter='vendor/thingsos-s3-reset.js')
