// browser_check.mjs — the page in a real (headless) browser, driven over
// the DevTools protocol: load it, let the machine boot, ask the page's
// window.__ms() for the frame count and the picture's colours, type a
// Return at RT-11's date prompt, expect the monitor's text on a black
// screen; then have the page type DIR and expect the listing (more text).
// Needs a Chromium-family browser started with --remote-debugging-port
// (see the CI job) and a server for dist/.
//
//   node src/web/browser_check.mjs http://localhost:8515/ [ws port]
const url = process.argv[2] ?? "http://localhost:8515/";
const port = process.argv[3] ?? "9222";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function target() {
  for (let i = 0; i < 300; ++i) {          // up to a minute: a CI runner's browser starts slowly
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      const page = list.find((t) => t.type === "page");
      if (page) return page.webSocketDebuggerUrl;
    } catch {}
    await sleep(200);
  }
  throw new Error("no debuggable page");
}

const ws = new WebSocket(await target());
await new Promise((ok) => ws.addEventListener("open", ok));
let seq = 0;
const pending = new Map();
ws.addEventListener("message", (m) => {
  const msg = JSON.parse(m.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); }
});
const send = (method, params = {}) => new Promise((ok) => {
  const id = ++seq;
  pending.set(id, ok);
  ws.send(JSON.stringify({ id, method, params }));
});
const evaluate = async (expression) => {
  const r = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
  if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.text + " " + JSON.stringify(r.result.exceptionDetails.exception?.description));
  return r.result?.result?.value;
};
const white = (peek) => peek.hist[0xffffffff] ?? 0;
const black = (peek) => peek.hist[0xff000000] ?? 0;

await send("Page.enable");
await send("Runtime.enable");
await send("Page.navigate", { url: url + (url.includes("?") ? "&" : "?") + "autostart=1" });
let peek = null;
for (let i = 0; i < 60; ++i) {
  await sleep(500);
  peek = await evaluate("window.__ms ? window.__ms() : null").catch(() => null);
  if (peek && peek.frames > 300) break;
}
if (!peek) throw new Error("the page never exposed __ms (a script error?)");
console.log(`after boot: frames ${peek.frames}, running ${peek.running}, colours ${peek.colours}, status "${peek.status}"`);

// RT-11's date prompt: a Return through the browser's key events.
await send("Input.dispatchKeyEvent", { type: "keyDown", code: "Enter", key: "Enter", windowsVirtualKeyCode: 13 });
await sleep(100);
await send("Input.dispatchKeyEvent", { type: "keyUp", code: "Enter", key: "Enter", windowsVirtualKeyCode: 13 });
await sleep(3000);
peek = await evaluate("window.__ms()");
const textBefore = white(peek);
console.log(`at the prompt: frames ${peek.frames}, colours ${peek.colours}, black ${black(peek)}, white ${textBefore}`);

// The page's typing: DIR lists the disk - more text on the screen.
await evaluate('window.__ms.type("DIR\\r")');
await sleep(5000);
peek = await evaluate("window.__ms()");
console.log(`after DIR: frames ${peek.frames}, white ${white(peek)}`);

// SHOT_SIZE=1920x1080 takes the screenshot at that viewport (a layout check).
const size = /^(\d+)x(\d+)$/.exec(process.env.SHOT_SIZE ?? "");
if (size) {
  await send("Emulation.setDeviceMetricsOverride",
             { width: +size[1], height: +size[2], deviceScaleFactor: 1, mobile: false });
  await sleep(500);
}
const shot = await send("Page.captureScreenshot", { format: "png" });
if (shot.result?.data) {
  const { writeFileSync } = await import("node:fs");
  writeFileSync("browser_check.png", Buffer.from(shot.result.data, "base64"));
}
ws.close();
if (!(peek.frames > 300 && black(peek) > 150000 && textBefore > 500))
  throw new Error("expected RT-11's monitor text on a black screen");
if (!(white(peek) > textBefore * 2))
  throw new Error("typing DIR did not bring a listing");
console.log("browser check OK");
