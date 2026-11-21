"""Package a private OTA beta and its verified return package; never publish or flash."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import zipfile

NATIVE = Path(__file__).resolve().parent
ROOT = NATIVE.parent
WORKSPACE = ROOT.parent

def sha(data):
    return hashlib.sha256(data).hexdigest()

def header(data):
    assert len(data) >= 288 and data[0] == 0xe9
    assert struct.unpack_from('<H', data, 12)[0] == 9
    assert struct.unpack_from('<I', data, 32)[0] == 0xabcd5432
    field = lambda at: data[at:at+32].split(b'\0')[0].decode('ascii')
    assert field(80) == 'ThingsOS_native'
    return field(48)

def package(version):
    assert re.fullmatch(r'\d+\.\d+\.\d+-beta\.\d+', version) and len(version) <= 31
    build = ROOT / f'build/native-idf-{version}'
    beta = (build / 'ThingsOS_native.bin').read_bytes()
    assert header(beta) == version
    published = ROOT / 'build/native-site-current'
    release = json.loads((published / 'release.json').read_text(encoding='utf-8'))
    assert release['version'] == '0.3.6' and release['layout'] == 'thingsos-native-single-v2'
    original = (published / release['ota']['path']).read_bytes()
    assert header(original) == '0.3.6' and sha(original) == release['ota']['sha256']
    assert len(original) == release['ota']['size']
    original_source = published / release['source']['path']
    assert sha(original_source.read_bytes()) == release['source']['sha256']
    # The existing 0.3.6 rescue must be able to install this application layout.
    usb = (published / release['usb']['path']).read_bytes()
    assert sha(usb) == release['usb']['sha256']
    table = (build / 'partition_table/partition-table.bin').read_bytes()
    assert table == usb[0x8000:0x8000+len(table)], 'Partition migration is not supported by this OTA beta'
    tests = subprocess.run([str(WORKSPACE / '.tools/idf-tools/tools/cmake/4.0.3/bin/ctest.exe'),
        '--test-dir', str(ROOT / 'build/native-host-cmake'), '--output-on-failure'],
        check=True, capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    assert '100% tests passed' in tests
    out = ROOT / f'build/beta/ThingsOS-{version}-pico'
    out.mkdir(parents=True, exist_ok=False)
    beta_name = f'ThingsOS-{version}-{sha(beta)[:12]}-ota.bin'
    original_name = f'ThingsOS-0.3.6-{sha(original)[:12]}-return-ota.bin'
    (out / beta_name).write_bytes(beta)
    (out / original_name).write_bytes(original)
    (out / 'host-tests.txt').write_text(tests, encoding='utf-8')
    (out / '先读我-刷入与测试.md').write_text(
        (NATIVE / 'beta-testing.md').read_text(encoding='utf-8').replace('0.3.6-beta.2', version), encoding='utf-8')
    shutil.copyfile(out / '先读我-刷入与测试.md', out / '先读我-刷入与测试.txt')
    shutil.copyfile(NATIVE / 'capture_beta_log.ps1', out / 'capture-log.ps1')
    shutil.copyfile(published / 'THIRD_PARTY_NOTICES.txt', out / 'THIRD_PARTY_NOTICES.txt')
    shutil.copyfile(original_source, out / original_source.name)
    source_name = f'ThingsOS-{version}-source.zip'
    official = WORKSPACE / 'references/read-pico-official'
    with zipfile.ZipFile(out / source_name, 'w', zipfile.ZIP_DEFLATED) as archive:
        def tree(folder, prefix):
            for path in sorted(folder.rglob('*')):
                relative = path.relative_to(folder)
                if any(p in {'build','__pycache__','.git','.netlify','node_modules','managed_components'} for p in relative.parts):
                    continue
                if path.is_file() and path.name != '.git' and path.suffix not in {'.bin','.elf','.map','.log','.pyc'} and path.name != 'sdkconfig.old':
                    archive.write(path, (Path(prefix) / relative).as_posix())
        tree(NATIVE, 'ThingsOS/native')
        for name in ('read_pico','epdiy','e0470_epaper_waveform','cst836u','fca9555','sy7636a','read_pico_pmu','sc7a20h','pwm_audio'):
            tree(official / 'components' / name, 'references/read-pico-official/components/' + name)
        for name in ('LICENSE','AGENTS.md','upstream-sources.json'):
            archive.write(ROOT / name, 'ThingsOS/' + name)
        archive.write(WORKSPACE / 'fonts/方正屏显雅宋.TTF', 'fonts/方正屏显雅宋.TTF')
        archive.write(ROOT / 'tools/audit_source_origins.py', 'ThingsOS/tools/audit_source_origins.py')
        archive.writestr('BETA-BUILD.txt', f'ESP-IDF v6.1. Build: .\\native\\build.ps1 -Version {version}\n'
            'Private testing snapshot. No device acceptance. Use the existing compatible 0.3.6 rescue.\n')
    manifest = dict(version=version, board='Read Pico RDP-G01-W', chip='ESP32-S3', project='ThingsOS_native',
        layout=release['layout'], channel='private-beta', device_validation='not performed',
        beta=beta_name, return_firmware=original_name,
        changes=['Rebuild failed font, then fall back to built-in font for the current session; persistent glyph failures retain navigation',
                 'Treat synthetic bold as optional; retry broken hint programs without hinting',
                 'Allocate FreeType memory in PSRAM and retain failed-frame cache guards and diagnostics'],
        files={})
    for path in sorted(out.iterdir()):
        data = path.read_bytes()
        manifest['files'][path.name] = dict(bytes=len(data), sha256=sha(data))
    (out / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    (out / 'SHA256SUMS.txt').write_text(''.join(f'{sha(p.read_bytes())}  {p.name}\n' for p in sorted(out.iterdir()) if p.is_file()), encoding='utf-8')
    # Version has dots; append the suffix rather than replacing its final segment.
    bundle = Path(str(out)+'.zip')
    with zipfile.ZipFile(bundle, 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(out.iterdir()):
            archive.write(path, path.name)
    with zipfile.ZipFile(bundle) as archive:
        assert archive.testzip() is None
        assert archive.read(beta_name) == beta and archive.read(original_name) == original
        assert len(archive.namelist()) == len(set(archive.namelist()))
    result = dict(directory=str(out), bundle=str(bundle), beta=beta_name,
        sha256=sha(beta), bundle_sha256=sha(bundle.read_bytes()), bytes=len(beta))
    (ROOT / f'build/beta/{version}-validation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', default='0.3.6-beta.2')
    package(parser.parse_args().version)
