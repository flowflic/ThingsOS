// SPDX-License-Identifier: Apache-2.0
(() => {
  const button = document.getElementById('feedback-toggle');
  const panel = document.getElementById('feedback-survey');
  if (!button || !panel) return;
  const frame = panel.querySelector('iframe');
  const label = button.querySelector('[data-feedback-label]');
  const arrow = button.querySelector('[aria-hidden]');
  const update = () => {
    if (panel.hidden) return;
    // Tencent's desktop form requires a wide frame. Phones and narrow windows
    // use the original survey instead of clipping fields or attachment controls.
    const embedded = panel.clientWidth >= 1024;
    panel.classList.toggle('is-embedded', embedded);
    if (embedded && !frame.hasAttribute('src')) frame.src = frame.dataset.surveySrc;
  };
  button.addEventListener('click', () => {
    const opened = panel.hidden;
    panel.hidden = !opened;
    button.setAttribute('aria-expanded', String(opened));
    label.textContent = opened ? '收起问卷' : '填写使用反馈';
    arrow.textContent = opened ? '↑' : '↓';
    update();
  });
  window.addEventListener('resize', update, { passive: true });
})();
