// Builds the phone app into one gzipped page plus manifest and icons, and puts
// them where the firmware's filesystem image is made from (firmware/data/).
// Also writes app/dist/ uncompressed, which the mock staff serves.

import { mkdir, rm } from "node:fs/promises";
import { join } from "node:path";
import * as zlib from "node:zlib";

// node:zlib hands back Buffers; the rest of the script works with plain bytes.
const bytes = (b: Buffer) => new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
const gzipSync = (input: string | Uint8Array) =>
  bytes(zlib.gzipSync(typeof input === "string" ? new TextEncoder().encode(input) : input, { level: 9 }));
const deflateSync = (input: Uint8Array) => bytes(zlib.deflateSync(input, { level: 9 }));

const root = join(import.meta.dir, "..");
const src = join(root, "app/src");
const dist = join(root, "app/dist");
const data = join(root, "firmware/data");

const js = await Bun.build({ entrypoints: [join(src, "main.ts")], minify: true, target: "browser" });
if (!js.success) {
  for (const log of js.logs) console.error(log);
  process.exit(1);
}
const script = await js.outputs[0].text();
const css = (await Bun.file(join(src, "styles.css")).text())
  .replace(/\/\*[\s\S]*?\*\//g, "")
  .replace(/\s+/g, " ")
  .replace(/\s*([{}:;,>])\s*/g, "$1")
  .trim();
const html = (await Bun.file(join(src, "index.html")).text())
  .replace("/*__CSS__*/", () => css)
  .replace("/*__JS__*/", () => script);

const manifest = JSON.stringify({
  name: "Kupferstab",
  short_name: "Kupferstab",
  start_url: "/",
  display: "standalone",
  background_color: "#16110e",
  theme_color: "#16110e",
  icons: [
    { src: "/icon.svg", sizes: "any", type: "image/svg+xml" },
    { src: "/icon-192.png", sizes: "192x192", type: "image/png" },
    { src: "/icon-512.png", sizes: "512x512", type: "image/png" },
  ],
});

const iconSvg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="#16110e"/><rect x="27" y="8" width="10" height="48" rx="5" fill="#e07f43"/><g fill="#ffd2a8"><circle cx="32" cy="17" r="2"/><circle cx="32" cy="27" r="2"/><circle cx="32" cy="37" r="2"/><circle cx="32" cy="47" r="2"/></g></svg>`;

const files: Record<string, Uint8Array | string> = {
  "index.html": html,
  "manifest.json": manifest,
  "icon.svg": iconSvg,
  "icon-180.png": iconPng(180),
  "icon-192.png": iconPng(192),
  "icon-512.png": iconPng(512),
};

await rm(dist, { recursive: true, force: true });
await rm(data, { recursive: true, force: true });
await mkdir(dist, { recursive: true });
await mkdir(data, { recursive: true });
let total = 0;
for (const [name, content] of Object.entries(files)) {
  await Bun.write(join(dist, name), content);
  // PNGs are compressed already; everything else is served gzipped by the staff.
  if (name.endsWith(".png")) {
    await Bun.write(join(data, name), content);
    total += typeof content === "string" ? content.length : content.byteLength;
  } else {
    const gz = gzipSync(content);
    await Bun.write(join(data, `${name}.gz`), gz);
    total += gz.byteLength;
  }
}
const htmlGz = gzipSync(html).byteLength;
console.log(`App gebaut: index.html ${(htmlGz / 1024).toFixed(1)} KB gzip, alles zusammen ${(total / 1024).toFixed(1)} KB`);
if (htmlGz > 60 * 1024) {
  console.error("index.html ist größer als 60 KB gzip");
  process.exit(1);
}

// The staff icon as a PNG for the home screen: copper bar with four light dots on dark ground.
function iconPng(size: number): Uint8Array {
  const px = new Uint8Array(size * (size * 4 + 1));
  const s = size / 64;
  const inRect = (x: number, y: number, x0: number, y0: number, w: number, h: number, r: number) => {
    const cx = Math.max(x0 + r, Math.min(x, x0 + w - r));
    const cy = Math.max(y0 + r, Math.min(y, y0 + h - r));
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r;
  };
  for (let y = 0; y < size; y++) {
    px[y * (size * 4 + 1)] = 0;
    for (let x = 0; x < size; x++) {
      const u = (x + 0.5) / s;
      const v = (y + 0.5) / s;
      let c = [0x16, 0x11, 0x0e];
      if (inRect(u, v, 27, 8, 10, 48, 5)) c = [0xe0, 0x7f, 0x43];
      for (const dy of [17, 27, 37, 47]) if ((u - 32) ** 2 + (v - dy) ** 2 <= 4) c = [0xff, 0xd2, 0xa8];
      const o = y * (size * 4 + 1) + 1 + x * 4;
      px.set([c[0], c[1], c[2], 255], o);
    }
  }
  const chunk = (type: string, body: Uint8Array) => {
    const out = new Uint8Array(12 + body.length);
    const dv = new DataView(out.buffer);
    dv.setUint32(0, body.length);
    out.set(new TextEncoder().encode(type), 4);
    out.set(body, 8);
    dv.setUint32(8 + body.length, crc32(out.subarray(4, 8 + body.length)));
    return out;
  };
  const ihdr = new Uint8Array(13);
  const dv = new DataView(ihdr.buffer);
  dv.setUint32(0, size);
  dv.setUint32(4, size);
  ihdr.set([8, 6, 0, 0, 0], 8);
  const parts = [
    new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(px)),
    chunk("IEND", new Uint8Array()),
  ];
  const png = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let off = 0;
  for (const p of parts) {
    png.set(p, off);
    off += p.length;
  }
  return png;
}

function crc32(buf: Uint8Array) {
  let c = ~0;
  for (const b of buf) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  }
  return ~c >>> 0;
}
