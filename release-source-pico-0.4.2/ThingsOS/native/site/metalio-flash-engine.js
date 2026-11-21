// SPDX-License-Identifier: MIT
// ThingsOS Metalio USB installer for the ESP32-S31.
// Uses the official esptool-js 0.7.0 S31 support and a verified Espressif stub.

import { ESPLoader, Transport } from './vendor/metalio/esptool.js';
import { resetS31AfterInstall } from './vendor/metalio/thingsos-s31-reset.js';

const S31_VID = 0x303a;
const S31_PID = 0x1001;
const S31_FLASH_SIZE = 0x1000000;

async function closePort(transport) {
  let timer;
  try {
    await Promise.race([
      transport.disconnect(),
      new Promise((_, reject) => {
        timer = setTimeout(() => {
          const error = new Error('串口关闭超时。请断开 USB 并刷新网页后重试。');
          error.releasePending = true;
          reject(error);
        }, 8000);
      })
    ]);
  } finally {
    clearTimeout(timer);
  }
}

function isDisconnect(error) {
  return error?.name === 'NetworkError' || /device has been lost|disconnected|port is closed/i.test(String(error?.message ?? ''));
}

function withTimeout(promise, milliseconds, message) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(message)), milliseconds);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

async function prepareS31Download(loader) {
  if (loader.secureDownloadMode) throw new Error('设备处于安全下载模式，无法安装。');
  if (loader.syncStubDetected) throw new Error('检测到已运行的下载程序，请重新进入 BOOT 模式后再连接。');
  if (loader.transport.getVid?.() !== S31_VID || loader.transport.getPid?.() !== S31_PID) {
    throw new Error('请选择 ESP32-S31 的 USB JTAG 下载口（303a:1001）。');
  }
  // S31 LP watchdog/SWD unlock sequence from the Espressif esptool 5.5.0 register map.
  await loader.writeReg(0x20801018, 0x50d83aa1);
  await loader.writeReg(0x20801000, 0);
  await loader.writeReg(0x20801018, 0);
  await loader.writeReg(0x20801020, 0x50d83aa1);
  const swd = await loader.readReg(0x2080101c);
  await loader.writeReg(0x2080101c, (swd | 0x40000) >>> 0);
  await loader.writeReg(0x20801020, 0);
}

async function prepareS31Flash(loader) {
  if (loader.chip?.IMAGE_CHIP_ID !== 32 || !loader.IS_STUB) throw new Error('S31 下载程序未就绪。');
  const request = new Uint8Array(24);
  const view = new DataView(request.buffer);
  [0, S31_FLASH_SIZE, 0x10000, 0x1000, 0x100, 0xffff].forEach((value, index) => view.setUint32(index * 4, value, true));
  const result = await loader.checkCommand('set SPI params', 0x0b, request, undefined, undefined, 5000);
  if (result !== 0) throw new Error('S31 Flash 参数初始化失败。');
}

function decodeBase64(text) {
  return Uint8Array.from(atob(text), (character) => character.charCodeAt(0));
}

async function runVerifiedStub(loader, stub) {
  if (loader.chip?.CHIP_NAME !== 'ESP32-S31' || loader.chip?.IMAGE_CHIP_ID !== 32) {
    throw new Error('未连接到支持的 ESP32-S31 下载口。');
  }
  const block = loader.ESP_RAM_BLOCK;
  const chunks = [
    [stub.text_start, decodeBase64(stub.text)],
    [stub.data_start, decodeBase64(stub.data)]
  ];
  for (const [address, bytes] of chunks) {
    if (!bytes.length) continue;
    await loader.memBegin(bytes.length, Math.ceil(bytes.length / block), block, address);
    for (let offset = 0, sequence = 0; offset < bytes.length; offset += block, sequence += 1) {
      await loader.memBlock(bytes.subarray(offset, offset + block), sequence);
    }
  }
  await loader.memFinish(stub.entry);
  const hello = await loader.transport.read(5000);
  const expected = [79, 72, 65, 73]; // "OHAI"
  if (hello.length !== expected.length || !expected.every((value, index) => hello[index] === value)) {
    throw new Error('S31 下载程序启动握手失败。');
  }
  loader.IS_STUB = true;
  await loader.applyUsbFlashWriteSize();
  return loader.chip;
}

async function verifyPart(loader, part) {
  const actual = await loader.flashMd5sum(part.offset, part.size);
  if (String(actual).toLowerCase() !== part.md5) {
    throw new Error(`设备端 MD5 校验失败：${part.label}。请保持连接后重试。`);
  }
}

export async function writeMetalioFirmware(port, verified, report) {
  const transport = new Transport(port);
  const loader = new ESPLoader({
    transport,
    baudrate: 115200,
    romBaudrate: 115200,
    enableTracing: false,
    serialOptions: { bufferSize: 65536 },
    terminal: { clean() {}, write() {}, writeLine() {} }
  });

  const chunks = verified.parts.map((part) => ({
    ...part,
    data: verified.usb.slice(part.offset, part.offset + part.size)
  }));
  chunks.forEach((chunk) => {
    if (chunk.data.length !== chunk.size) throw new Error(`固件分区长度不符：${chunk.label}。`);
  });
  const totalBytes = chunks.reduce((sum, chunk) => sum + chunk.data.length, 0);

  loader.runStub = async function runStub() {
    await prepareS31Download(this);
    return runVerifiedStub(this, verified.stub);
  };

  let failure;
  let completed = false;
  try {
    report({ stage: 'connecting' });
    const main = loader.main('usb_reset');
    main.catch(() => {});
    await withTimeout(main, 30000, 'S31 握手超时，尚未写入。请重新进入 BOOT 模式后再连接。');
    if (loader.chip?.CHIP_NAME !== 'ESP32-S31' || loader.chip?.IMAGE_CHIP_ID !== 32 || !loader.IS_STUB) {
      throw new Error('未连接到支持的 ESP32-S31 下载口，尚未写入。');
    }
    console.info('[Metalio] connected', loader.chip.CHIP_NAME, loader.chip.IMAGE_CHIP_ID, 'stub', loader.IS_STUB);

    report({ stage: 'checking' });
    await prepareS31Flash(loader);

    report({ stage: 'writing', percentage: 0 });
    await loader.writeFlash({
      fileArray: chunks.map((chunk) => ({ data: chunk.data, address: chunk.offset })),
      flashSize: 'keep',
      flashMode: 'keep',
      flashFreq: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (index, sent, total) => {
        const prior = chunks.slice(0, index).reduce((sum, chunk) => sum + chunk.data.length, 0);
        const fraction = total > 0 ? sent / total : 0;
        const overall = (prior + chunks[index].data.length * fraction) / totalBytes;
        report({
          stage: 'writing',
          percentage: Math.min(90, Math.round(overall * 90)),
          detail: `正在写入 ${chunks[index].label}（${index + 1}/${chunks.length}）`
        });
      }
    });

    report({ stage: 'verifying', percentage: 90, detail: '正在读取设备 Flash 并核对 MD5…' });
    for (let index = 0; index < chunks.length; index += 1) {
      await verifyPart(loader, chunks[index]);
      report({
        stage: 'verifying',
        percentage: 90 + Math.round(((index + 1) / chunks.length) * 10),
        detail: `已校验 ${chunks[index].label}（${index + 1}/${chunks.length}）`
      });
    }

    report({ stage: 'restarting', percentage: 100, detail: '正在重启设备…' });
    await resetS31AfterInstall(loader);
    await new Promise((resolve) => setTimeout(resolve, 1200));
    completed = true;
  } catch (error) {
    failure = error;
  } finally {
    try {
      await closePort(transport);
    } catch (error) {
      if (completed && !error.releasePending) {
        // The reset deliberately disconnects the USB device. Ignore that close error.
      } else if (!failure || error.releasePending) {
        failure = error;
      }
    }
  }
  if (failure) throw failure;
  console.info('[Metalio] all parts written and verified');
  report({ stage: 'finished', percentage: 100, detail: '安装完成，设备正在重启。' });
}
