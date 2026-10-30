// A simulated staff for trying the app without hardware: serves app/dist/ and
// speaks the firmware's WebSocket protocol and /update. Run `bun run build`
// first, then `bun run mock` and open http://localhost:8080.

import { join } from "node:path";
import {
  sanitizeState,
  type ClientMessage,
  type Config,
  type LightState,
  type Snapshot,
} from "../src/protocol";

const dist = join(import.meta.dir, "../dist");
const port = Number(process.env.PORT ?? 8080);
const started = Date.now();

let state: LightState = {
  on: true,
  bri: 160,
  effect: "rainbow",
  speed: 50,
  color: "#FF6A1F",
  color2: "#1A0A00",
};
const scenes: (LightState | null)[] = [
  { ...state, effect: "fire", speed: 60 },
  { ...state, effect: "solid", color: "#0030FF" },
  null,
  null,
];
let config: Config = {
  leds: 60,
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
      state = { ...state, ...sanitizeState(msg.state) };
      return broadcast();
    case "scene":
      if (msg.op === "save") scenes[msg.slot] = { ...state };
      if (msg.op === "apply" && scenes[msg.slot]) state = { ...scenes[msg.slot]!, on: true };
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
