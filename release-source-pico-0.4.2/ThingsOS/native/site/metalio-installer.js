// SPDX-License-Identifier: MIT
import { writeMetalioFirmware } from './metalio-flash-engine.js';

const button = document.getElementById('metalio-install');
const consent = document.getElementById('metalio-consent');
const panel = document.getElementById('metalio-flash-progress');
const title = document.getElementById('metalio-flash-title');
const detail = document.getElementById('metalio-flash-detail');
const meter = document.getElementById('metalio-flash-meter');
const percent = document.getElementById('metalio-flash-percent');
const steps = [...document.querySelectorAll('[data-metalio-flash-stage]')];
const order = ['connecting', 'checking', 'writing', 'verifying', 'restarting'];
const messages = {
  selecting: ['选择设备串口', '请在浏览器的串口选择器中选择 Metalio 的 USB JTAG 下载口（303a:1001）。'],
  connecting: ['正在连接 S31', '正在通过 USB JTAG 建立连接并加载下载程序，请保持 USB 连接。'],
  checking: ['正在校验设备', '正在核对 S31 芯片并初始化 16 MB Flash。'],
  writing: ['正在写入固件', '正在写入引导、分区表、救援系统、主系统和字体，请勿拔线或关闭页面。'],
  verifying: ['正在校验固件', '正在从设备 Flash 读回每个分区并核对 MD5。'],
  restarting: ['正在重启设备', '固件写入并校验完成，正在请求设备重启。'],
  finished: ['安装完成', '重启指令已发送。请查看设备屏幕，等待 ThingsOS 启动。'],
  cancelled: ['已取消选择设备', '尚未写入任何内容。准备好后可重新连接。'],
  error: ['安装未完成', '']
};

function show({ stage, percentage = 0, message }) {
  console.info('[Metalio]', stage, percentage, message || '');
  panel.hidden = false;
  panel.dataset.state = stage;
  title.textContent = messages[stage][0];
  detail.textContent = message || messages[stage][1];
  if (stage === 'writing' || stage === 'verifying') {
    meter.hidden = false;
    meter.value = percentage;
    percent.textContent = `${percentage}%`;
  } else if (stage === 'restarting' || stage === 'finished') {
    meter.hidden = false;
    meter.value = 100;
    percent.textContent = '100%';
  } else {
    meter.removeAttribute('value');
    meter.hidden = ['error', 'cancelled', 'selecting'].includes(stage);
    percent.textContent = '';
  }
  const active = order.indexOf(stage);
  steps.forEach((element, index) => {
    element.dataset.state = stage === 'finished' || active > index ? 'done' : active === index ? 'active' : 'waiting';
    if (active === index) element.setAttribute('aria-current', 'step');
    else element.removeAttribute('aria-current');
  });
}

function sync() {
  const verified = globalThis.thingsOSVerifiedInstallationMetalio;
  const available = Boolean(globalThis.isSecureContext && 'serial' in navigator);
  document.getElementById('metalio-browser-notice').hidden = available;
  consent.disabled = Boolean(globalThis.thingsOSFlashBusy);
  button.disabled = !verified || !consent.checked || globalThis.thingsOSFlashBusy || !available;
}

globalThis.thingsOSSyncMetalioInstallation = sync;

consent.addEventListener('change', sync);
button.addEventListener('click', async () => {
  const verified = globalThis.thingsOSVerifiedInstallationMetalio;
  if (!verified || !consent.checked || globalThis.thingsOSFlashBusy || !navigator.serial) return;
  globalThis.thingsOSFlashBusy = true;
  sync();
  globalThis.thingsOSSyncInstallation?.();
  show({ stage: 'selecting' });
  let port;
  try {
    port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a, usbProductId: 0x1001 }] });
    await writeMetalioFirmware(port, verified, show);
  } catch (error) {
    console.error('[Metalio] install failed', error);
    if (error.releasePending) globalThis.thingsOSMetalioInstallerReady = false;
    if (!port && error.name === 'NotFoundError') show({ stage: 'cancelled' });
    else show({
      stage: 'error',
      message: `${error.message || error} 请确认设备已进入 USB 下载模式（按住顶部 BOOT 键再上电，或在「设置 → 恢复 → 进入 ROM USB 下载模式」切换后连接），并使用支持数据传输的 USB 线。`
    });
  } finally {
    globalThis.thingsOSFlashBusy = false;
    button.textContent = panel.dataset.state === 'finished' ? '重新连接并安装 ↗' : '连接 S31 并重试 ↗';
    sync();
    globalThis.thingsOSSyncInstallation?.();
  }
});

globalThis.thingsOSMetalioInstallerReady = true;
sync();
