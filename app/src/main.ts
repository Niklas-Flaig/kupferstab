import {
  EFFECTS,
  PALETTE_MAX,
  PALETTE_PRESETS,
  PINS,
  effectDef,
  effectivePercent,
  encode,
  formatParam,
  hexToHsv,
  hsvToHex,
  mergeState,
  parseServerMessage,
  updateTarget,
  type ClientMessage,
  type ConfigPatch,
  type EffectId,
  type Fx,
  type FxPatch,
  layoutText,
  type Snapshot,
  type StatePatch,
} from "./protocol";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const SWATCHES = ["#FF6A1F", "#FF2A10", "#FFB030", "#14C9A8", "#2A5BFF", "#4A5BFF", "#B04CFF", "#FF2A6D", "#FFFFFF"];

let snap: Snapshot | null = null;
let runAt = 0; // when the last snapshot with a running strobe arrived
let socket: WebSocket | null = null;
let retry = 1000;
let saveMode = false;
let selected = 0; // the palette colour the wheel edits

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
      runAt = performance.now();
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
let pending: StatePatch | null = null;
function mergePatch(a: StatePatch | null, b: StatePatch): StatePatch {
  const out: StatePatch = { ...a, ...b };
  if (a?.fx || b.fx) {
    out.fx = { ...a?.fx };
    for (const [id, f] of Object.entries(b.fx ?? {}) as [EffectId, FxPatch][]) {
      const prev = out.fx[id];
      out.fx[id] = { ...prev, ...f, p: { ...prev?.p, ...f.p } };
    }
  }
  return out;
}
function setState(patch: StatePatch) {
  if (snap) snap.state = mergeState(snap.state, patch);
  pending = mergePatch(pending, patch);
  requestAnimationFrame(() => {
    if (!pending) return;
    send({ t: "set", state: pending });
    pending = null;
  });
}

const current = (): { id: EffectId; fx: Fx } | null =>
  snap ? { id: snap.state.effect, fx: snap.state.fx[snap.state.effect] } : null;

function setFx(patch: FxPatch) {
  const c = current();
  if (c) setState({ on: true, fx: { [c.id]: patch } });
}

function setPaletteColor(color: string) {
  const c = current();
  if (!c) return;
  const palette = [...c.fx.palette];
  palette[Math.min(selected, palette.length - 1)] = color;
  setFx({ palette });
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
  setPaletteColor(hsvToHex(h, s, 1));
  render();
}

// touch-action: none alone does not stop every browser from scrolling or reloading under the finger.
wheel.addEventListener("touchmove", (ev) => ev.preventDefault(), { passive: false });
wheel.addEventListener("pointerdown", (ev) => {
  wheel.setPointerCapture(ev.pointerId);
  pickFromWheel(ev);
});
wheel.addEventListener("pointermove", (ev) => {
  if (wheel.hasPointerCapture(ev.pointerId)) pickFromWheel(ev);
});

// ---------- palette ----------

const paletteEl = $("palette");
function renderPalette(fx: Fx) {
  selected = Math.min(selected, fx.palette.length - 1);
  paletteEl.replaceChildren();
  fx.palette.forEach((c, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "pc";
    b.style.background = c;
    b.setAttribute("aria-label", `Farbe ${i + 1}`);
    b.setAttribute("aria-pressed", String(i === selected));
    b.addEventListener("click", () => {
      selected = i;
      render();
    });
    if (i === selected && fx.palette.length > 1) {
      const del = document.createElement("span");
      del.className = "del";
      del.textContent = "×";
      del.setAttribute("role", "button");
      del.setAttribute("aria-label", `Farbe ${i + 1} entfernen`);
      del.addEventListener("click", (ev) => {
        ev.stopPropagation();
        setFx({ palette: fx.palette.filter((_, j) => j !== i) });
        selected = Math.max(0, i - 1);
        render();
      });
      b.append(del);
    }
    paletteEl.append(b);
  });
  if (fx.palette.length < PALETTE_MAX) {
    const add = document.createElement("button");
    add.type = "button";
    add.className = "add";
    add.textContent = "+";
    add.setAttribute("aria-label", "Farbe hinzufügen");
    add.addEventListener("click", () => {
      const last = hexToHsv(fx.palette[fx.palette.length - 1]);
      setFx({ palette: [...fx.palette, hsvToHex((last.h + 50) % 360, Math.max(0.6, last.s), 1)] });
      selected = fx.palette.length;
      render();
    });
    paletteEl.append(add);
  }
}

const swatches = $("swatches");
for (const c of SWATCHES) {
  const b = document.createElement("button");
  b.style.background = c;
  b.setAttribute("aria-label", `Farbe ${c}`);
  b.addEventListener("click", () => {
    setPaletteColor(c);
    render();
  });
  swatches.append(b);
}

const presets = $("presets");
for (const pr of PALETTE_PRESETS) {
  const b = document.createElement("button");
  b.type = "button";
  const i = document.createElement("i");
  i.style.background = pr.colors.length === 1 ? pr.colors[0] : `linear-gradient(90deg, ${pr.colors.join(", ")})`;
  b.append(i, pr.name);
  b.addEventListener("click", () => {
    setFx({ palette: [...pr.colors] });
    selected = 0;
    render();
  });
  presets.append(b);
}

// ---------- effect ----------

const effectsEl = $("effects");
for (const e of EFFECTS) {
  const b = document.createElement("button");
  b.textContent = e.name;
  b.dataset.id = e.id;
  b.addEventListener("click", () => {
    if (e.id === "strobo") {
      // Starts the strobe, or starts it over; it returns to the current effect by itself.
      send({ t: "run" });
      if (snap) snap.state = mergeState(snap.state, { effect: "strobo", on: true });
    } else {
      setState({ effect: e.id, on: true });
    }
    render();
  });
  effectsEl.append(b);
}

const variantsEl = $("variants");
const paramsEl = $("params");
let shownEffect: EffectId | null = null;

// The sliders of the current effect: palette timing first, then the effect's own.
function buildParams(id: EffectId) {
  shownEffect = id;
  const def = effectDef(id);
  variantsEl.replaceChildren();
  def.variants.forEach((name, i) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "chip";
    b.textContent = name;
    b.dataset.i = String(i);
    b.addEventListener("click", () => {
      setFx({ variant: i });
      if (id === "strobo") send({ t: "run" });
      render();
    });
    variantsEl.append(b);
  });
  paramsEl.replaceChildren();
  const rows = [
    { key: "period", label: "Farbwechsel alle", min: 3, max: 60, step: 1, unit: "s", top: true },
    { key: "spread", label: "Verteilung über den Stab", min: 0, max: 100, step: 5, unit: "%", top: true },
    ...def.params.map((p) => ({ ...p, top: false })),
  ];
  for (const r of rows) {
    const label = document.createElement("label");
    label.className = "slider";
    label.dataset.key = r.key;
    label.dataset.top = String(r.top);
    const span = document.createElement("span");
    const out = document.createElement("output");
    span.append(r.label, " ", out);
    const input = document.createElement("input");
    input.type = "range";
    input.min = String(r.min);
    input.max = String(r.max);
    input.step = String(r.step);
    input.addEventListener("input", () => {
      const v = Number(input.value);
      setFx(r.top ? { [r.key]: v } : { p: { [r.key]: v } });
      out.textContent = formatParam(v, r.unit);
    });
    label.dataset.unit = r.unit;
    label.append(span, input);
    paramsEl.append(label);
  }
  variantsEl.hidden = def.variants.length < 2;
  $("whiteRow").hidden = id !== "strobo";
}

$<HTMLInputElement>("white").addEventListener("change", (ev) =>
  setFx({ white: (ev.target as HTMLInputElement).checked }),
);
$("runStop").addEventListener("click", () => {
  if (snap?.run) setState({ effect: snap.run.back });
  render();
});

// The strobe's progress, counted on locally from the last snapshot.
function renderRun() {
  const run = snap?.run;
  const box = $("run");
  if (!snap || !run || snap.state.effect !== "strobo") {
    box.hidden = true;
    return;
  }
  const p = snap.state.fx.strobo.p;
  const e = run.t + (performance.now() - runAt) / 1000;
  const ramp = p.charge + p.build;
  const back = effectDef(run.back).name;
  let phase: string, rest: number;
  if (e < ramp) [phase, rest] = ["Anlauf", ramp - e];
  else if (e < ramp + p.hold) [phase, rest] = ["Strobo", ramp + p.hold - e];
  else [phase, rest] = ["Ausklang", ramp + p.hold + p.after - e];
  box.hidden = false;
  $("runPhase").textContent = `${phase} · noch ${Math.max(0, Math.ceil(rest))} s`;
  $("runSub").textContent = `danach zurück zu ${back}`;
}
setInterval(renderRun, 250);

const bri = $<HTMLInputElement>("bri");
bri.addEventListener("input", () => {
  setState({ bri: Number(bri.value), on: true });
  renderOutputs();
});

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
}

function render() {
  if (!snap) return;
  const s = snap.state;
  const c = current()!;
  renderPalette(c.fx);
  drawWheel(c.fx.palette[selected] ?? c.fx.palette[0]);
  if (document.activeElement !== bri) bri.value = String(s.bri);
  renderOutputs();
  $("power").setAttribute("aria-pressed", String(s.on));
  for (const b of effectsEl.querySelectorAll<HTMLButtonElement>("button"))
    b.setAttribute("aria-pressed", String(b.dataset.id === s.effect));

  if (shownEffect !== c.id) buildParams(c.id);
  for (const b of variantsEl.querySelectorAll<HTMLButtonElement>("button"))
    b.setAttribute("aria-pressed", String(Number(b.dataset.i) === c.fx.variant));
  for (const label of paramsEl.querySelectorAll<HTMLLabelElement>("label")) {
    const key = label.dataset.key!;
    const top = label.dataset.top === "true";
    const v = top ? (c.fx[key as "period" | "spread"] as number) : c.fx.p[key];
    const input = label.querySelector("input")!;
    if (document.activeElement !== input) input.value = String(v);
    label.querySelector("output")!.textContent =
      key === "spread" && v === 0 ? "ganzer Stab gleich" : formatParam(v, label.dataset.unit!);
    label.classList.toggle("dim", top && c.fx.palette.length === 1);
  }
  $<HTMLInputElement>("white").checked = c.fx.white !== false;
  renderRun();

  snap.scenes.forEach((sc, i) => {
    const b = scenesEl.children[i] as HTMLButtonElement;
    b.dataset.empty = String(!sc);
    (b.querySelector(".dot") as HTMLElement).style.background = sc?.fx[sc.effect]?.palette[0] ?? "transparent";
    (b.lastElementChild as HTMLElement).textContent = sc ? effectDef(sc.effect).name : `Platz ${i + 1}`;
  });
  const info = snap.info;
  $("info").textContent =
    `Firmware ${info.fw} · ${Math.round(info.heap / 1024)} KB frei · läuft seit ${formatUptime(info.uptime)} · ` +
    `${info.clients} ${info.clients === 1 ? "Handy" : "Handys"} verbunden` +
    (info.staIp ? ` · Heim-WLAN ${info.staIp}` : "");
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

const ledsIn = $<HTMLInputElement>("leds");
const foldIn = $<HTMLInputElement>("fold");
const tipSel = $<HTMLSelectElement>("tip");
function showLayout() {
  $("layoutHint").textContent = layoutText(Number(ledsIn.value), Number(foldIn.value), tipSel.value === "1");
}
for (const el of [ledsIn, foldIn, tipSel]) el.addEventListener("input", showLayout);

function fillSettings() {
  if (!snap) return;
  const c = snap.config;
  ledsIn.value = String(c.leds);
  foldIn.value = String(c.fold ?? 0); // older firmware sends no layout
  tipSel.value = c.tip ? "1" : "0";
  showLayout();
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
    leds: Number(ledsIn.value),
    fold: Number(foldIn.value),
    tip: tipSel.value === "1",
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
