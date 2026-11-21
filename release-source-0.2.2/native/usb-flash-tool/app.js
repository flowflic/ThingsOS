const state = {
  since: 0,
  polling: null,
  artifacts: [],
  ports: [],
};

const el = {
  port: document.getElementById("port"),
  refresh: document.getElementById("refresh"),
  artifact: document.getElementById("artifact"),
  path: document.getElementById("path"),
  selector: document.getElementById("selector"),
  start: document.getElementById("start"),
  stage: document.getElementById("stage"),
  progress: document.getElementById("progress"),
  verdict: document.getElementById("verdict"),
  log: document.getElementById("log"),
  evidence: document.getElementById("evidence"),
  env: document.getElementById("env"),
};

function setBusy(busy) {
  el.start.disabled = busy;
  el.port.disabled = busy;
  el.artifact.disabled = busy;
  el.path.disabled = busy;
  el.selector.disabled = busy;
  el.refresh.disabled = busy;
  el.start.textContent = busy ? "烧录中…" : "开始烧录并检查";
}

function append(text) {
  const nearBottom =
    el.log.scrollHeight - el.log.scrollTop - el.log.clientHeight < 40;
  el.log.textContent += text;
  if (nearBottom) el.log.scrollTop = el.log.scrollHeight;
}

function describePort(port) {
  const id = [port.vid, port.pid].filter(Boolean).join(":");
  const parts = [port.device];
  if (id) parts.push(id);
  if (port.serialNumber) parts.push(port.serialNumber);
  if (port.isTarget) parts.push("ESP32-S31");
  return parts.join(" · ");
}

function renderPorts(ports) {
  state.ports = ports;
  const previous = el.port.value;
  el.port.innerHTML = "";
  if (!ports.length) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "未发现串口";
    el.port.append(option);
    return;
  }
  for (const port of ports) {
    const option = document.createElement("option");
    option.value = port.device;
    option.dataset.serial = port.serialNumber || "";
    option.textContent = describePort(port);
    el.port.append(option);
  }
  const target = ports.find((p) => p.device === previous) || ports.find((p) => p.isTarget) || ports[0];
  el.port.value = target.device;
}

function renderArtifacts(artifacts) {
  state.artifacts = artifacts;
  el.artifact.innerHTML = "";
  if (!artifacts.length) {
    const option = document.createElement("option");
    option.value = "";
    option.textContent = "未找到可用镜像";
    el.artifact.append(option);
    return;
  }
  for (const artifact of artifacts) {
    const option = document.createElement("option");
    option.value = artifact.label;
    option.textContent = `${artifact.label} · v${artifact.version} · ${artifact.mtime}`;
    el.artifact.append(option);
  }
  el.artifact.value = artifacts[0].label;
  el.path.value = artifacts[0].label;
}

async function loadState() {
  const response = await fetch("/api/state");
  const data = await response.json();
  el.env.textContent = `Python ${data.python} · esptool ${data.esptool}\n日志最多读取 ${data.captureSeconds} 秒`;
  renderPorts(data.ports);
  renderArtifacts(data.artifacts);
  setBusy(Boolean(data.busy));
  if (data.busy) {
    el.stage.textContent = "已有任务在进行";
    pollEvents();
  }
}

function renderVerdict(result) {
  if (!result) {
    el.verdict.hidden = true;
    return;
  }
  el.verdict.hidden = false;
  el.verdict.className = `verdict ${result.status === "ok" ? "ok" : "fail"}`;
  el.verdict.innerHTML = "";
  const title = document.createElement("strong");
  title.textContent = result.message;
  el.verdict.append(title);
  const list = document.createElement("ul");
  for (const check of result.checks || []) {
    const item = document.createElement("li");
    item.className = check.ok ? "good" : "bad";
    item.textContent = `${check.ok ? "通过" : "未通过"} · ${check.name} · ${check.detail}`;
    list.append(item);
  }
  el.verdict.append(list);
  if (result.image && result.image.sha256) {
    const meta = document.createElement("li");
    meta.textContent = `SHA-256 ${result.image.sha256}`;
    meta.style.listStyle = "none";
    meta.style.marginTop = "6px";
    meta.style.opacity = ".7";
    list.append(meta);
  }
  el.evidence.textContent = result.evidence ? `证据目录 ${result.evidence}` : "";
}

async function pollEvents() {
  if (state.polling) return;
  state.polling = window.setInterval(async () => {
    try {
      const response = await fetch(`/api/events?since=${state.since}`);
      const data = await response.json();
      for (const event of data.events) {
        if (event.kind === "device") {
          append(event.text);
        } else if (event.kind === "stage") {
          el.stage.textContent = event.text;
          append(`\n== ${event.text} ==\n`);
          el.progress.textContent = "";
        } else if (event.kind === "progress") {
          const match = event.text.match(/(\d{1,3})\s*%/);
          el.progress.textContent = match ? `${match[1]}%` : event.text;
        } else {
          append(`${event.text}\n`);
        }
      }
      state.since = data.seq;
      if (!data.running) {
        stopPolling();
        setBusy(false);
        el.progress.textContent = "";
        renderVerdict(data.result);
        if (data.result) {
          el.stage.textContent = data.result.status === "ok" ? "完成" : "未通过";
        }
      }
    } catch (error) {
      append(`\n[页面] 无法读取进度：${error}\n`);
      stopPolling();
      setBusy(false);
    }
  }, 250);
}

function stopPolling() {
  if (state.polling) {
    window.clearInterval(state.polling);
    state.polling = null;
  }
}

async function startFlash() {
  const port = el.port.value;
  const image = el.path.value.trim();
  if (!port) {
    el.stage.textContent = "请选择串口";
    return;
  }
  if (!image) {
    el.stage.textContent = "请选择镜像";
    return;
  }
  el.log.textContent = "";
  el.progress.textContent = "";
  el.evidence.textContent = "";
  renderVerdict(null);
  state.since = 0;
  setBusy(true);
  el.stage.textContent = "启动任务";

  const option = el.port.selectedOptions[0];
  try {
    const response = await fetch("/api/flash", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        port,
        image,
        serialNumber: option ? option.dataset.serial : "",
        writeSelector: el.selector.checked,
      }),
    });
    if (!response.ok) {
      const data = await response.json().catch(() => ({}));
      throw new Error(data.error || `HTTP ${response.status}`);
    }
    pollEvents();
  } catch (error) {
    append(`[页面] ${error.message}\n`);
    setBusy(false);
    el.stage.textContent = "未能启动任务";
  }
}

el.refresh.addEventListener("click", loadState);
el.artifact.addEventListener("change", () => {
  el.path.value = el.artifact.value;
});
el.start.addEventListener("click", startFlash);

loadState().catch((error) => {
  el.stage.textContent = "无法连接本地服务";
  append(`[页面] ${error}\n`);
});
