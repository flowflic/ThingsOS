// SPDX-License-Identifier: MIT
const tabs = [...document.querySelectorAll('[data-device]')];
const panels = [...document.querySelectorAll('[data-device-panel]')];

function select(device, focus = false) {
  tabs.forEach((tab) => {
    const active = tab.dataset.device === device;
    tab.classList.toggle('is-active', active);
    tab.setAttribute('aria-selected', String(active));
    tab.tabIndex = active ? 0 : -1;
    if (active && focus) tab.focus();
  });
  panels.forEach((panel) => {
    panel.hidden = panel.dataset.devicePanel !== device;
  });
  globalThis.thingsOSSelectedDevice = device;
  if (device === 'metalio') {
    globalThis.thingsOSVerifyMetalio?.();
    globalThis.thingsOSSyncMetalioInstallation?.();
  } else {
    globalThis.thingsOSSyncInstallation?.();
  }
}

tabs.forEach((tab) => {
  tab.addEventListener('click', () => select(tab.dataset.device));
});
tabs.forEach((tab, index) => {
  tab.addEventListener('keydown', (event) => {
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return;
    event.preventDefault();
    const next = tabs[(index + (event.key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length];
    select(next.dataset.device, true);
  });
});

select('pico');
