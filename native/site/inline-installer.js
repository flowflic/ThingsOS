// SPDX-License-Identifier: Apache-2.0
import { writeFirmware } from './flash-engine.js';
const button = document.getElementById('install');
const panel = document.getElementById('flash-progress');
const title = document.getElementById('flash-title');
const detail = document.getElementById('flash-detail');
const meter = document.getElementById('flash-meter');
const percent = document.getElementById('flash-percent');
const steps = [...document.querySelectorAll('[data-flash-stage]')];
const order = ['connecting', 'erasing', 'writing', 'restarting'];
const messages = {
  selecting: ['选择设备串口', '请在浏览器的串口选择器中选择阅读器。'],
  connecting: ['正在连接阅读器', '正在识别芯片并建立连接，请保持 USB 连接。'],
  erasing: ['正在擦除 Flash', '正在清除设备内部 Flash，请勿拔线或关闭页面。'],
  writing: ['正在更新固件', '正在写入 ThingsOS，请保持本页可见并等待完成。'],
  restarting: ['正在重启阅读器', '固件写入完成，正在请求设备重启。'],
  finished: ['固件写入完成', '重启指令已发送。请查看设备屏幕，等待 ThingsOS 启动。'],
  cancelled: ['已取消选择设备', '尚未擦除或写入。准备好后可重新连接。'],
  error: ['安装未完成', '']
};
function show({ stage, percentage, message }) {
  panel.hidden = false; panel.dataset.state = stage;
  title.textContent = messages[stage][0]; detail.textContent = message || messages[stage][1];
  const writing = stage === 'writing';
  if (writing || stage === 'finished') meter.value = writing ? percentage : 100;
  else meter.removeAttribute('value');
  meter.hidden = ['error', 'cancelled', 'selecting'].includes(stage);
  percent.textContent = writing ? `${percentage}%` : stage === 'finished' ? '100%' : '';
  const active = order.indexOf(stage);
  steps.forEach((el, i) => {
    el.dataset.state = stage === 'finished' || active > i ? 'done' : active === i ? 'active' : 'waiting';
    if (active === i) el.setAttribute('aria-current', 'step'); else el.removeAttribute('aria-current');
  });
}
if (!globalThis.isSecureContext || !('serial' in navigator)) {
  document.getElementById('browser-notice').hidden = false;
}
button.addEventListener('click', async () => {
  const verified = globalThis.thingsOSVerifiedInstallation;
  if (!verified || !document.getElementById('consent').checked || globalThis.thingsOSFlashBusy || !navigator.serial) return;
  globalThis.thingsOSFlashBusy = true;
  globalThis.thingsOSSyncInstallation();
  document.getElementById('consent').disabled = true;
  show({ stage: 'selecting' });
  let port;
  try {
    // Keep requestPort directly inside the click gesture. Only this browser-owned chooser opens.
    port = await navigator.serial.requestPort();
    await writeFirmware(port, verified.usb, show);
  } catch (error) {
    if (error.releasePending) globalThis.thingsOSInstallerReady = false;
    if (!port && error.name === 'NotFoundError') show({ stage: 'cancelled' });
    else show({ stage: 'error', message: `${error.message || error} 请检查 USB 连接；若设备未启动，可按住 BOOT 进入下载模式后重新安装。` });
  } finally {
    globalThis.thingsOSFlashBusy = false;
    document.getElementById('consent').disabled = false;
    button.textContent = panel.dataset.state === 'finished' ? '重新连接并安装 ↗' : '连接设备并重试 ↗';
    globalThis.thingsOSSyncInstallation();
  }
});

globalThis.thingsOSInstallerReady = true;
globalThis.thingsOSSyncInstallation();
