import {
  EFFECTS,
  PINS,
  effectivePercent,
  encode,
  hexToHsv,
  hsvToHex,
  parseServerMessage,
  updateTarget,
  wifiStatus,
  type ClientMessage,
  type ConfigPatch,
  type LightState,
  type Snapshot,
} from "./protocol";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const SWATCHES = ["#FF6A1F", "#FF0000", "#FFB000", "#00FF40", "#00E5FF", "#0030FF", "#9000FF", "#FF00A0", "#FFFFFF"];

let snap: Snapshot | null = null;
let socket: WebSocket | null = null;
let retry = 1000;
let saveMode = false;

// ---------- connection ----------

function connect() {
  setConn("connecting", "Verbinde …");
  const ws = new WebSocket(`ws://${location.host}/ws`);
  socket = ws;
  ws.onopen = () => {
    retry = 1000;
    setConn("open", "Verbunden");
  };
  ws.onmessage = (ev) => {
    const msg = parseServerMessage(String(ev.data));
    if (!msg) return;
    if (msg.t === "state") {
      const first = !snap;
      snap = msg;
      render();
      if (first && dialog.open) fillSettings();
    } else {
      toast(msg.message);
    }
  };
  ws.onclose = () => {
    if (socket !== ws) return;
    setConn("closed", "Getrennt");
    setTimeout(connect, retry);
    retry = Math.min(retry * 2, 5000);
  };
}

function setConn(state: "connecting" | "open" | "closed", label: string) {
  const el = $("conn");
  el.dataset.state = state;
  el.textContent = label;
  document.body.classList.toggle("offline", state !== "open");
}

function send(msg: ClientMessage) {
  if (socket?.readyState === WebSocket.OPEN) socket.send(encode(msg));
}

// Sliders and the wheel fire many events; send at most one change per frame.
let pending: Partial<LightState> | null = null;
function setState(patch: Partial<LightState>) {
  if (snap) Object.assign(snap.state, patch);
  pending = { ...pending, ...patch };
  requestAnimationFrame(() => {
    if (!pending) return;
    send({ t: "set", state: pending });
    pending = null;
  });
}

// ---------- colour wheel ----------

const wheel = $<HTMLCanvasElement>("wheel");
const ctx = wheel.getContext("2d")!;
const R = wheel.width / 2;
let wheelBase: ImageData | null = null;

function drawWheelBase() {
  const img = ctx.createImageData(wheel.width, wheel.height);
  for (let y = 0; y < wheel.height; y++) {
    for (let x = 0; x < wheel.width; x++) {
      const dx = x - R;
      const dy = y - R;
      const d = Math.hypot(dx, dy) / R;
      const i = (y * wheel.width + x) * 4;
      if (d > 1) continue;
      const h = ((Math.atan2(dy, dx) * 180) / Math.PI + 360) % 360;
      const n = parseInt(hsvToHex(h, d, 1).slice(1), 16);
      img.data[i] = (n >> 16) & 255;
      img.data[i + 1] = (n >> 8) & 255;
      img.data[i + 2] = n & 255;
      img.data[i + 3] = d > 0.985 ? Math.round((1 - d) / 0.015 * 255) : 255;
    }
  }
  wheelBase = img;
}

function drawWheel(color: string) {
  if (!wheelBase) drawWheelBase();
  ctx.putImageData(wheelBase!, 0, 0);
  const { h, s } = hexToHsv(color);
  const a = (h * Math.PI) / 180;
  const x = R + Math.cos(a) * s * R * 0.94;
  const y = R + Math.sin(a) * s * R * 0.94;
  ctx.lineWidth = 6;
  ctx.strokeStyle = "#fff";
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.arc(x, y, 26, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.lineWidth = 2;
  ctx.strokeStyle = "rgba(0,0,0,.45)";
  ctx.beginPath();
  ctx.arc(x, y, 30, 0, Math.PI * 2);
  ctx.stroke();
}

function pickFromWheel(ev: PointerEvent) {
  const rect = wheel.getBoundingClientRect();
  const dx = ((ev.clientX - rect.left) / rect.width) * 2 - 1;
  const dy = ((ev.clientY - rect.top) / rect.height) * 2 - 1;
  const s = Math.min(1, Math.hypot(dx, dy) / 0.94);
  const h = ((Math.atan2(dy, dx) * 180) / Math.PI + 360) % 360;
  const color = hsvToHex(h, s, 1);
  // Picking a colour while a colourful effect runs would show nothing; switch to plain colour.
  const effect = snap && ["rainbow", "colorloop", "fire"].includes(snap.state.effect) ? "solid" : undefined;
  setState(effect ? { color, effect, on: true } : { color, on: true });
  render();
}

wheel.addEventListener("pointerdown", (ev) => {
  wheel.setPointerCapture(ev.pointerId);
  pickFromWheel(ev);
});
wheel.addEventListener("pointermove", (ev) => {
  if (wheel.hasPointerCapture(ev.pointerId)) pickFromWheel(ev);
});

// ---------- controls ----------

const swatches = $("swatches");
for (const c of SWATCHES) {
  const b = document.createElement("button");
  b.style.background = c;
  b.setAttribute("aria-label", `Farbe ${c}`);
  b.addEventListener("click", () => {
    setState({ color: c, on: true });
    render();
  });
  swatches.append(b);
}

const effectsEl = $("effects");
for (const e of EFFECTS) {
  const b = document.createElement("button");
  b.textContent = e.name;
  b.dataset.id = e.id;
  b.addEventListener("click", () => {
    setState({ effect: e.id, on: true });
    render();
  });
  effectsEl.append(b);
}

const bri = $<HTMLInputElement>("bri");
bri.addEventListener("input", () => {
  setState({ bri: Number(bri.value), on: true });
  renderOutputs();
});
const speed = $<HTMLInputElement>("speed");
speed.addEventListener("input", () => {
  setState({ speed: Number(speed.value) });
  renderOutputs();
});
const color2 = $<HTMLInputElement>("color2");
color2.addEventListener("input", () => setState({ color2: color2.value.toUpperCase() }));

$("power").addEventListener("click", () => {
  if (!snap) return;
  setState({ on: !snap.state.on });
  render();
});

const scenesEl = $("scenes");
for (let i = 0; i < 4; i++) {
  const b = document.createElement("button");
  b.innerHTML = `<span class="dot"></span><span>Szene ${i + 1}</span>`;
  b.addEventListener("click", () => {
    if (saveMode) {
      send({ t: "scene", slot: i, op: "save" });
      toast(`Szene ${i + 1} gespeichert`);
      setSaveMode(false);
    } else if (snap?.scenes[i]) {
      send({ t: "scene", slot: i, op: "apply" });
    } else {
      toast("Leerer Platz: erst „Speichern“ tippen, dann den Platz wählen.");
    }
  });
  scenesEl.append(b);
}
function setSaveMode(on: boolean) {
  saveMode = on;
  $("saveMode").setAttribute("aria-pressed", String(on));
  document.body.classList.toggle("saving", on);
  $("sceneHint").textContent = on
    ? "Wähle den Platz, auf den der aktuelle Stand soll."
    : "Tippen ruft eine Szene ab. „Speichern“ legt den aktuellen Stand auf einen Platz.";
}
$("saveMode").addEventListener("click", () => setSaveMode(!saveMode));

// ---------- render ----------

function renderOutputs() {
  if (!snap) return;
  const pct = effectivePercent(snap.state.bri, snap.config.maxBri);
  const capped = snap.state.bri > snap.config.maxBri;
  $("briOut").textContent = `${pct} %${capped ? " (Grenze)" : ""}`;
  $("speedOut").textContent = String(snap.state.speed);
}

function render() {
  if (!snap) return;
  const s = snap.state;
  drawWheel(s.color);
  if (document.activeElement !== bri) bri.value = String(s.bri);
  if (document.activeElement !== speed) speed.value = String(s.speed);
  if (document.activeElement !== color2) color2.value = s.color2.toLowerCase();
  renderOutputs();
  $("power").setAttribute("aria-pressed", String(s.on));
  for (const b of effectsEl.querySelectorAll<HTMLButtonElement>("button"))
    b.setAttribute("aria-pressed", String(b.dataset.id === s.effect));
  snap.scenes.forEach((sc, i) => {
    const b = scenesEl.children[i] as HTMLButtonElement;
    b.dataset.empty = String(!sc);
    (b.querySelector(".dot") as HTMLElement).style.background = sc ? sc.color : "transparent";
    (b.lastElementChild as HTMLElement).textContent = sc
      ? EFFECTS.find((e) => e.id === sc.effect)?.name ?? `Szene ${i + 1}`
      : `Platz ${i + 1}`;
  });
  const info = snap.info;
  $("info").textContent =
    `Firmware ${info.fw} · ${Math.round(info.heap / 1024)} KB frei · läuft seit ${formatUptime(info.uptime)} · ` +
    `${info.clients} ${info.clients === 1 ? "Handy" : "Handys"} verbunden` +
    (info.staIp ? ` · Hotspot ${info.staIp}` : "");
  $("wifiStatus").textContent = wifiStatus(info, snap.config);
  const install = $<HTMLButtonElement>("installUpdate");
  install.hidden = !info.update;
  install.textContent = `Version ${info.update} installieren`;
}

function formatUptime(sec: number) {
  if (sec < 60) return `${sec} s`;
  if (sec < 3600) return `${Math.floor(sec / 60)} min`;
  return `${Math.floor(sec / 3600)} h ${Math.floor((sec % 3600) / 60)} min`;
}

let toastTimer = 0;
function toast(text: string) {
  const el = $("toast");
  el.textContent = text;
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => (el.hidden = true), 3200);
}

// ---------- settings ----------

const dialog = $<HTMLDialogElement>("settings");
const pinSel = $<HTMLSelectElement>("pin");
for (const p of PINS) pinSel.add(new Option(p.label, String(p.gpio)));
const maxBri = $<HTMLInputElement>("maxBri");
maxBri.addEventListener("input", () => {
  $("maxBriOut").textContent = `${Math.round((Number(maxBri.value) / 255) * 100)} %`;
});

function fillSettings() {
  if (!snap) return;
  const c = snap.config;
  $<HTMLInputElement>("leds").value = String(c.leds);
  pinSel.value = String(c.pin);
  $<HTMLSelectElement>("order").value = c.order;
  maxBri.value = String(c.maxBri);
  maxBri.dispatchEvent(new Event("input"));
  $<HTMLInputElement>("apSsid").value = c.apSsid;
  $<HTMLInputElement>("staSsid").value = c.staSsid;
  $<HTMLInputElement>("apPass").value = "";
  $<HTMLInputElement>("staPass").value = "";
}

function openSettings() {
  fillSettings();
  if (!dialog.open) dialog.showModal();
}
$("openSettings").addEventListener("click", openSettings);
// http://192.168.4.1/#einstellungen opens the sheet directly, e.g. as a bookmark.
if (location.hash === "#einstellungen") openSettings();

$("nextPin").addEventListener("click", () => {
  const i = PINS.findIndex((p) => String(p.gpio) === pinSel.value);
  const next = PINS[(i + 1) % PINS.length];
  pinSel.value = String(next.gpio);
  send({ t: "config", config: { pin: next.gpio } });
  toast(`Teste ${next.label} …`);
});
pinSel.addEventListener("change", () => send({ t: "config", config: { pin: Number(pinSel.value) } }));
$("test").addEventListener("click", () => send({ t: "test" }));

$("saveSettings").addEventListener("click", () => {
  const form = $<HTMLFormElement>("settingsForm");
  if (!form.reportValidity()) return;
  const patch: ConfigPatch = {
    leds: Number($<HTMLInputElement>("leds").value),
    pin: Number(pinSel.value),
    order: $<HTMLSelectElement>("order").value as "GRB" | "RGB",
    maxBri: Number(maxBri.value),
    apSsid: $<HTMLInputElement>("apSsid").value.trim(),
    staSsid: $<HTMLInputElement>("staSsid").value.trim(),
  };
  const apPass = $<HTMLInputElement>("apPass").value;
  const staPass = $<HTMLInputElement>("staPass").value;
  if (apPass) patch.apPass = apPass;
  if (staPass) patch.staPass = staPass;
  send({ t: "config", config: patch });
  toast("Gespeichert");
});

// ---------- update ----------

$("checkUpdate").addEventListener("click", () => {
  send({ t: "checkUpdate" });
  toast("Frage GitHub …");
});
$("installUpdate").addEventListener("click", () => send({ t: "installUpdate" }));

$<HTMLInputElement>("updateFile").addEventListener("change", async (ev) => {
  const input = ev.target as HTMLInputElement;
  const file = input.files?.[0];
  if (!file) return;
  const status = $("updateStatus");
  const bar = $<HTMLProgressElement>("updateProgress");
  const first = new Uint8Array(await file.slice(0, 1).arrayBuffer())[0];
  const target = updateTarget(file.name, first);
  if (!target) {
    status.dataset.kind = "bad";
    status.textContent = "Datei nicht erkannt. Erwartet: firmware.bin oder littlefs.bin.";
    return;
  }
  const body = new FormData();
  body.append("file", file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/update?target=${target}`);
  xhr.upload.onprogress = (e) => {
    if (e.lengthComputable) bar.value = Math.round((e.loaded / e.total) * 100);
  };
  xhr.onload = () => {
    bar.hidden = true;
    if (xhr.status === 200) {
      status.dataset.kind = "ok";
      status.textContent = `${target === "fs" ? "App" : "Firmware"} aktualisiert. Der Stab startet neu und verbindet sich gleich wieder.`;
    } else {
      status.dataset.kind = "bad";
      status.textContent = `Update fehlgeschlagen: ${xhr.responseText || xhr.status}`;
    }
    input.value = "";
  };
  xhr.onerror = () => {
    bar.hidden = true;
    status.dataset.kind = "bad";
    status.textContent = "Verbindung abgebrochen. Prüfe das WLAN und versuch es noch einmal.";
    input.value = "";
  };
  bar.hidden = false;
  bar.value = 0;
  status.dataset.kind = "";
  status.textContent = `Lade ${file.name} hoch (${target === "fs" ? "App" : "Firmware"}) …`;
  xhr.send(body);
});

drawWheel("#FF6A1F");
connect();
