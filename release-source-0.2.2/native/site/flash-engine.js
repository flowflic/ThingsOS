// SPDX-License-Identifier: Apache-2.0
import { Transport, ESPLoader } from './vendor/esptool.js';
import { resetS3AfterInstall } from './vendor/thingsos-s3-reset.js';

async function closePort(transport) {
  let timer;
  try {
    await Promise.race([transport.disconnect(), new Promise((_, reject) => {
      timer = setTimeout(() => {
        const error = Error('串口关闭超时。请断开 USB 并刷新网页后重试。');
        error.releasePending = true; reject(error);
      }, 8000);
    })]);
  } finally { clearTimeout(timer); }
}

// The caller supplies the exact bytes already checked by verify-release.js.
export async function writeFirmware(port, bytes, onState, dependencies = { Transport, ESPLoader, resetS3AfterInstall }) {
  const transport = new dependencies.Transport(port);
  const loader = new dependencies.ESPLoader({ transport, baudrate: 115200, enableTracing: false });
  let stage = 'connecting', failure;
  try {
    onState({ stage });
    await loader.main();
    if (loader.chip?.CHIP_NAME !== 'ESP32-S3') throw Error('设备芯片不匹配，需要 ESP32-S3。未擦除或写入。');
    await loader.flashId();
    stage = 'erasing'; onState({ stage });
    await loader.eraseFlash();
    stage = 'writing'; onState({ stage, percentage: 0 });
    await loader.writeFlash({
      fileArray: [{ data: new Uint8Array(bytes), address: 0 }],
      flashSize: 'keep', flashMode: 'keep', flashFreq: 'keep', eraseAll: false, compress: true,
      reportProgress: (_index, written, total) => onState({ stage, percentage: Math.min(100, Math.floor(written / total * 100)) })
    });
    stage = 'restarting'; onState({ stage });
    await dependencies.resetS3AfterInstall(loader);
  } catch (error) {
    failure = error;
  } finally {
    // Release the serial port on failure as well as success, so a retry can connect.
    try { await closePort(transport); }
    catch (error) { if (!failure || error.releasePending) failure = error; }
  }
  if (failure) throw failure;
  onState({ stage: 'finished' });
}
