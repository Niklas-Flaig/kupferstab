// A simulated staff for trying the app without hardware: serves app/dist/ and
// speaks the firmware's WebSocket protocol and /update. Run `bun run build`
// first, then `bun run mock` and open http://localhost:8080.

import { join } from "node:path";
import {
  mergeState,
  sanitizeState,
  type ClientMessage,
  type Config,
  type EffectId,
  type LightState,
  type SceneState,
  type Snapshot,
} from "../src/protocol";

const dist = join(import.meta.dir, "../dist");
const port = Number(process.env.PORT ?? 8080);
const started = Date.now();

// The firmware's defaults (kEffects in firmware/src/config.cpp).
let state: LightState = {
  on: true,
  bri: 160,
  effect: "atem",
  fx: {
    atem: { variant: 0, palette: ["#4A5BFF", "#14C9A8", "#B04CFF"], period: 14, spread: 35, p: { breath: 7, wave: 4.5, width: 12 } },
    strobo: { variant: 0, palette: ["#14C9A8", "#2A5BFF"], period: 10, spread: 90, p: { charge: 2, build: 4, hold: 5, after: 5 }, white: true },
    glut: { variant: 0, palette: ["#FF6A1F"], period: 12, spread: 30, p: { tempo: 50 } },
    polarlicht: { variant: 0, palette: ["#FF6A1F"], period: 12, spread: 30, p: { tempo: 50 } },
    funkenflug: { variant: 0, palette: ["#FF6A1F"], period: 12, spread: 30, p: { tempo: 50 } },
    mosaik: { variant: 0, palette: ["#FF2A6D", "#B04CFF", "#2A5BFF", "#14C9A8"], period: 16, spread: 50, p: { tempo: 50, density: 50 } },
  },
};
const sceneOf = (s: LightState): SceneState => ({ on: s.on, bri: s.bri, effect: s.effect, fx: { [s.effect]: s.fx[s.effect] } });
const scenes: (SceneState | null)[] = [sceneOf({ ...state, effect: "glut" }), sceneOf(state), null, null];

// The strobe run, as the staff keeps it: when it started and where it returns to.
let runStart = 0;
let back: EffectId = "atem";
function startRun() {
  if (state.effect !== "strobo") back = state.effect;
  state = { ...state, effect: "strobo", on: true };
  runStart = Date.now();
}
setInterval(() => {
  if (state.effect !== "strobo") return;
  const p = state.fx.strobo.p;
  if ((Date.now() - runStart) / 1000 >= p.charge + p.build + p.hold + p.after) {
    state = { ...state, effect: back };
    broadcast();
  }
}, 200);

let config: Config = {
  leds: 164,
  fold: 50,
  tip: true,
  pin: 2,
  order: "GRB",
  maxBri: 102,
  apSsid: "Kupferstab",
  staSsid: "",
};

let update = "";

const clients = new Set<import("bun").ServerWebSocket<unknown>>();

function snapshot(): Snapshot {
  return {
    t: "state",
    state,
    scenes,
    run: state.effect === "strobo" ? { t: (Date.now() - runStart) / 1000, back } : null,
    config,
    info: {
      fw: "0.1.0-mock",
      heap: 23456,
      uptime: Math.floor((Date.now() - started) / 1000),
      apIp: "192.168.4.1",
      staIp: "",
      update,
      clients: clients.size,
      app: true,
    },
  };
}

const broadcast = () => {
  const text = JSON.stringify(snapshot());
  for (const c of clients) c.send(text);
};

function handle(ws: import("bun").ServerWebSocket<unknown>, msg: ClientMessage) {
  switch (msg.t) {
    case "get":
      return ws.send(JSON.stringify(snapshot()));
    case "set":
    {
      const patch = sanitizeState(msg.state);
      if (patch.effect === "strobo" && state.effect !== "strobo") startRun();
      state = mergeState(state, { ...patch, effect: patch.effect ?? state.effect });
      return broadcast();
    }
    case "run":
      startRun();
      return broadcast();
    case "scene":
      if (msg.op === "save") scenes[msg.slot] = sceneOf(state);
      if (msg.op === "apply" && scenes[msg.slot]) {
        const sc = scenes[msg.slot]!;
        state = { ...state, on: true, bri: sc.bri, effect: sc.effect, fx: { ...state.fx, [sc.effect]: sc.fx[sc.effect] } };
      }
      if (msg.op === "clear") scenes[msg.slot] = null;
      return broadcast();
    case "config": {
      const { apPass, staPass, otaPass, ...rest } = msg.config;
      const wifi = (rest.apSsid && rest.apSsid !== config.apSsid) || apPass || staPass || otaPass;
      config = { ...config, ...rest };
      if (wifi)
        ws.send(JSON.stringify({ t: "notice", message: "Gespeichert. Der Stab startet neu, verbinde dich danach wieder." }));
      console.log("[mock] config", msg.config);
      return broadcast();
    }
    case "test":
      console.log("[mock] test chase");
      return;
    case "reboot":
      console.log("[mock] reboot");
      return;
    case "checkUpdate":
      update = "0.2.0";
      for (const c of clients) c.send(JSON.stringify({ t: "notice", message: "Version 0.2.0 ist verfügbar. Installieren unter Einstellungen → Update." }));
      return broadcast();
    case "installUpdate":
      console.log("[mock] install", update);
      for (const c of clients) c.send(JSON.stringify({ t: "notice", message: `Installiere Version ${update} … Der Stab startet danach neu.` }));
      update = "";
      return broadcast();
  }
}

Bun.serve({
  port,
  async fetch(req, server) {
    const url = new URL(req.url);
    if (url.pathname === "/ws") {
      return server.upgrade(req) ? undefined : new Response("WebSocket erwartet", { status: 400 });
    }
    if (url.pathname === "/api/state") return Response.json(snapshot());
    if (url.pathname === "/update" && req.method === "POST") {
      const form = await req.formData();
      const file = form.get("file");
      const size = file instanceof Blob ? file.size : 0;
      console.log(`[mock] update target=${url.searchParams.get("target")} size=${size}`);
      return new Response(size ? "OK" : "Keine Datei", { status: size ? 200 : 500 });
    }
    const path = url.pathname === "/" ? "/index.html" : url.pathname;
    const file = Bun.file(join(dist, path));
    return (await file.exists()) ? new Response(file) : new Response("Nicht gefunden", { status: 404 });
  },
  websocket: {
    open(ws) {
      clients.add(ws);
      broadcast();
    },
    message(ws, text) {
      try {
        handle(ws, JSON.parse(String(text)) as ClientMessage);
      } catch {
        ws.send(JSON.stringify({ t: "error", message: "Nachricht nicht lesbar" }));
      }
    },
    close(ws) {
      clients.delete(ws);
    },
  },
});

console.log(`Simulierter Stab: http://localhost:${port}`);
