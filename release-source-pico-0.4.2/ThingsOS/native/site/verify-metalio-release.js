// SPDX-License-Identifier: MIT
// ThingsOS Metalio preview: verify the S31 release before enabling USB install.
// This file is intentionally independent from the Read Pico verifier.

const RELEASE_URL = './metalio/release.json';
const MANIFEST_URL = './metalio/manifest.json';
const BIN_PATH = /^firmware\/metalio\/[A-Za-z0-9_.-]+\.bin$/;
const STUB_PATH = /^vendor\/metalio\/esp32s31-stub\.json$/;

const status = document.getElementById('metalio-status');
const consent = document.getElementById('metalio-consent');
const install = document.getElementById('metalio-install');
const browserNotice = document.getElementById('metalio-browser-notice');
const downloads = document.getElementById('metalio-downloads');
const usbLink = document.getElementById('metalio-usb');
const otaLink = document.getElementById('metalio-ota');
const sha = document.getElementById('metalio-sha');

let valid = false;
let started = false;

function sync() {
  const busy = Boolean(globalThis.thingsOSFlashBusy);
  install.disabled = !valid || !consent.checked || busy;
  consent.disabled = busy;
}

function setStatus(message, tone = '') {
  status.textContent = message;
  if (tone) status.dataset.tone = tone;
  else delete status.dataset.tone;
}

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function assetUrl(path, pattern) {
  assert(typeof path === 'string' && pattern.test(path) && !path.startsWith('/') && !path.includes('..'), '安装包路径无效。');
  return './' + path;
}

async function fetchAsset(path, pattern) {
  const response = await fetch(assetUrl(path, pattern), { cache: 'no-store' });
  if (!response.ok) throw new Error(`无法下载 ${path}（HTTP ${response.status}）。`);
  return new Uint8Array(await response.arrayBuffer());
}

async function fetchJson(path) {
  const response = await fetch(path, { cache: 'no-store' });
  if (!response.ok) throw new Error(`无法读取 ${path}（HTTP ${response.status}）。`);
  return response.json();
}

async function sha256(bytes) {
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return Array.from(new Uint8Array(digest), (byte) => byte.toString(16).padStart(2, '0')).join('');
}

function u16(bytes, offset) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint16(offset, true);
}

function u32(bytes, offset) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(offset, true);
}

function textAt(bytes, offset, length) {
  let end = offset;
  while (end < offset + length && bytes[end] !== 0) end += 1;
  return new TextDecoder().decode(bytes.slice(offset, end));
}

function checkPartitionTable(usb) {
  const expected = [
    ['nvs', 1, 2, 0x9000, 0x6000],
    ['otadata', 1, 0, 0xf000, 0x2000],
    ['phy_init', 1, 1, 0x11000, 0x1000],
    ['rescue', 0, 0, 0x20000, 0x200000],
    ['ota_0', 0, 0x10, 0x220000, 0xae0000],
    ['system_font', 1, 0x40, 0xd00000, 0x300000]
  ];
  for (let index = 0; index < expected.length; index += 1) {
    const [label, type, subtype, offset, size] = expected[index];
    const base = 0x8000 + index * 32;
    assert(u16(usb, base) === 0x50aa, '分区表标识无效。');
    assert(usb[base + 2] === type && usb[base + 3] === subtype, `分区类型不符：${label}。`);
    assert(u32(usb, base + 4) === offset && u32(usb, base + 8) === size, `分区地址或大小不符：${label}。`);
    assert(textAt(usb, base + 12, 16) === label, `分区名称不符：${label}。`);
  }
}

function checkAppImage(usb, version) {
  assert(usb[0x220000] === 0xe9, '主系统镜像标识无效。');
  assert(u32(usb, 0x220000 + 12) === 32, '主系统芯片标识不是 ESP32-S31。');
  assert(u32(usb, 0x220000 + 32) === 0xabcd5432, '主系统应用描述符无效。');
  assert(textAt(usb, 0x220000 + 48, 32) === version, '主系统版本不符。');
  assert(textAt(usb, 0x220000 + 80, 32) === 'ThingsOS_native', '主系统项目名不符。');
}

function checkParts(release, usb, ota) {
  const expectedOffsets = [0x2000, 0x8000, 0x9000, 0xf000, 0x20000, 0x220000, 0xd00000];
  const parts = release.parts;
  assert(Array.isArray(parts) && parts.length === expectedOffsets.length, '分区清单不完整。');
  parts.forEach((part, index) => {
    assert(part && part.offset === expectedOffsets[index], '分区地址不符。');
    assert(Number.isInteger(part.size) && part.size > 0, '分区大小无效。');
    assert(part.offset + part.size <= usb.length, '分区超出固件范围。');
    assert(index === 0 || parts[index - 1].offset + parts[index - 1].size <= part.offset, '分区发生重叠。');
    assert(/^[0-9a-f]{64}$/.test(part.sha256), '分区 SHA-256 无效。');
    assert(/^[0-9a-f]{32}$/.test(part.md5), '分区 MD5 无效。');
  });
  assert(parts[1].size === 4096, '分区表大小不符。');
  assert(parts[2].offset === 0x9000 && parts[2].size === 0x6000, 'NVS 分区不符。');
  assert(parts[3].offset === 0xf000 && parts[3].size === 0x2000, '启动记录分区不符。');
  assert(parts[5].offset === 0x220000 && parts[5].size === ota.length, '主系统分区大小与 OTA 文件不符。');
  assert(parts[5].sha256 === release.ota.sha256, '主系统分区哈希与 OTA 文件不符。');
  assert(parts[6].offset + parts[6].size === usb.length, '系统字体分区未覆盖到固件末尾。');
}

async function validate(release, manifest, usb, ota, stubRaw) {
  assert(release && typeof release === 'object', '安装包元数据无效。');
  assert(release.schema === 2, '安装包版本不受支持。');
  assert(release.project === 'ThingsOS_native', '安装包项目不匹配。');
  assert(release.device === 'Metalio E-Ink4-Plus' || release.board === 'Metalio-E-Ink4-Plus', '安装包不是 Metalio E-Ink4-Plus。');
  assert(release.board === 'Metalio-E-Ink4-Plus', '安装包设备标识不匹配。');
  assert(release.chip === 'ESP32-S31', '安装包芯片不是 ESP32-S31。');
  assert(release.layout === 'thingsos-metalio-single-v1', '安装包分区布局不受支持。');
  assert(release.status === 'release', '安装包状态不是正式版。');
  assert(release.firmware_ready === true, '安装包状态异常。');
  assert(typeof release.version === 'string' && release.version === manifest.version, '固件与清单版本不一致。');
  assert(release.usb && release.usb.size === usb.length, 'USB 固件大小不符。');
  assert(release.ota && release.ota.size === ota.length, 'OTA 固件大小不符。');
  assert((await sha256(usb)) === release.usb.sha256, 'USB 固件 SHA-256 校验失败。');
  assert((await sha256(ota)) === release.ota.sha256, 'OTA 固件 SHA-256 校验失败。');
  assert((await sha256(stubRaw)) === release.stub.sha256, 'S31 下载程序 SHA-256 校验失败。');

  const manifestPart = manifest.builds?.[0]?.parts?.[0];
  assert(Array.isArray(manifest.builds) && manifest.builds.length === 1, '安装清单必须包含一个构建。');
  assert(manifest.builds[0].chipFamily === 'ESP32-S31', '安装清单芯片不是 ESP32-S31。');
  assert(manifestPart && manifestPart.offset === 0 && manifestPart.path === release.usb.path, '安装清单与 USB 固件不一致。');
  assert(manifest.new_install_prompt_erase === true, '安装清单缺少擦除确认。');

  assert(usb.length >= 0xd00000, 'USB 固件长度不足。');
  assert(usb.findIndex((byte) => byte !== 0xff) === 0x2000, 'USB 固件起始区域不是空白。');
  assert(usb[0x2000] === 0xe9 && u32(usb, 0x2000 + 12) === 32, '引导器镜像标识不符。');
  assert(usb[0x8000] === 0xaa && usb[0x8001] === 0x50, '分区表魔数不符。');
  assert(u32(usb, 0xf000) === 1 && u32(usb, 0xf000 + 24) === 0 && u32(usb, 0xf000 + 28) === 0x4743989a, '启动记录无效。');
  checkPartitionTable(usb);
  checkAppImage(usb, release.version);
  checkParts(release, usb, ota);

  for (const part of release.parts) {
    const slice = usb.slice(part.offset, part.offset + part.size);
    assert((await sha256(slice)) === part.sha256, `分区 SHA-256 校验失败：${part.label}。`);
    if (part.label === 'app') assert(part.size === ota.length, '主系统分区大小与 OTA 不一致。');
  }

  const stub = JSON.parse(new TextDecoder().decode(stubRaw));
  assert(stub.entry === 788538252 && stub.text_start === 788533248 && stub.data_start === 788647352 && stub.bss_start === 788570112, 'S31 下载程序地址不符。');
  assert(typeof stub.text === 'string' && typeof stub.data === 'string' && stub.text.length > 0 && stub.data.length > 0, 'S31 下载程序内容不完整。');

  return { release, manifest, usb, ota, stub, parts: release.parts.map((part) => ({ ...part })) };
}

async function start() {
  if (started) return;
  started = true;
  try {
    setStatus('正在下载并校验 Metalio 安装包…');
    const [release, manifest] = await Promise.all([fetchJson(RELEASE_URL), fetchJson(MANIFEST_URL)]);
    assert(release.usb && release.ota && release.stub, '安装包元数据缺少固件条目。');
    const [usb, ota, stubRaw] = await Promise.all([
      fetchAsset(release.usb.path, BIN_PATH),
      fetchAsset(release.ota.path, BIN_PATH),
      fetchAsset(release.stub.path, STUB_PATH)
    ]);
    const verified = await validate(release, manifest, usb, ota, stubRaw);

    globalThis.thingsOSVerifiedInstallationMetalio = verified;
    valid = true;

    usbLink.href = assetUrl(release.usb.path, BIN_PATH);
    usbLink.download = release.usb.path.split('/').pop();
    otaLink.href = assetUrl(release.ota.path, BIN_PATH);
    otaLink.download = release.ota.path.split('/').pop();
    sha.textContent = `USB  ${release.usb.sha256}\nOTA  ${release.ota.sha256}`;
    downloads.hidden = false;

    if (!globalThis.isSecureContext || !('serial' in navigator)) browserNotice.hidden = false;
    setStatus('安装包已校验。确认设备后即可连接安装。');
  } catch (error) {
    valid = false;
    globalThis.thingsOSVerifiedInstallationMetalio = null;
    setStatus(`安装包检查失败：${error.message}`, 'error');
  }
  sync();
  globalThis.thingsOSSyncMetalioInstallation?.();
}

globalThis.thingsOSVerifyMetalio = start;
globalThis.thingsOSSyncMetalioInstallation = sync;

consent.addEventListener('change', sync);
sync();

if (!globalThis.isSecureContext || !('serial' in navigator)) browserNotice.hidden = false;
if (globalThis.thingsOSSelectedDevice === 'metalio') start();
