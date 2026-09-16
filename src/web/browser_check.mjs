// browser_check.mjs — the page in a real (headless) browser, driven over
// the DevTools protocol: load it, let the machine boot, ask the page's
// window.__ms() for the frame count and the picture's colours, type a
// Return at RT-11's date prompt, expect the monitor's text on a black
// screen; then have the page type DIR and expect the listing (more text);
// last, have it pack a bug report and check what went into it.
// Needs a Chromium-family browser started with --remote-debugging-port
// (see the CI job) and a server for src/, so that the page can take its disk
// list from web/test-disks/ (a test fixture) instead of the collection's Pages.
//
//   node src/web/browser_check.mjs "http://localhost:8515/build/emscripten-release/web/dist/?disks=/web/test-disks/" [ws port]
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

// Wait until the picture stops changing and `ok` is happy with it.
// Counting frames or sleeping a fixed number of seconds is a guess about
// how fast the processor is, and such a guess stopped being true the
// moment the processor started taking the time its documentation gives:
// the machine was still booting when the check typed at it, and the
// listing never came.  Asking the screen costs nothing and cannot go stale.
//
// Note the polarity: an empty page is white all over, and it is the booted
// machine that paints most of the screen black with white text on it.
async function settle(what, ok = () => true, tries = 160) {
  let last = -1, same = 0, peek = null;
  for (let i = 0; i < tries; ++i) {
    await sleep(500);
    peek = await evaluate("window.__ms ? window.__ms() : null").catch(() => null);
    if (!peek) continue;
    const now = white(peek);
    same = now === last ? same + 1 : 0;
    last = now;
    if (same >= 3 && ok(peek)) return peek;
  }
  if (!peek) throw new Error("the page never exposed __ms (a script error?)");
  throw new Error(`the screen never settled on ${what}: `
                  + `white ${last}, black ${peek ? black(peek) : "?"}`);
}

// The machine's own screen is up: black ground, not the page's blank white -
// and the machine running, because a canvas that has never been drawn on is
// black all over too, which used to pass this and let the check press a
// button at a page that was still loading its module.
const painted = (p) => black(p) > 150000 && p.frames > 0;

await send("Page.enable");
await send("Runtime.enable");
await send("Page.navigate", { url: url + (url.includes("?") ? "&" : "?") + "autostart=1" });
let peek = await settle("the date prompt", painted);
console.log(`after boot: frames ${peek.frames}, running ${peek.running}, colours ${peek.colours}, status "${peek.status}"`);

// Sound is on when the page opens.  Before anything has been touched a
// browser allows no more than a suspended context, so that is what this
// asserts; the Return below is the gesture that starts it for real.
if (peek.sound === "off") throw new Error("the page opened with the sound off");
console.log(`sound before a gesture: ${peek.sound}`);

// RT-11's date prompt: a Return through the browser's key events.
await send("Input.dispatchKeyEvent", { type: "keyDown", code: "Enter", key: "Enter", windowsVirtualKeyCode: 13 });
await sleep(100);
await send("Input.dispatchKeyEvent", { type: "keyUp", code: "Enter", key: "Enter", windowsVirtualKeyCode: 13 });
peek = await settle("the monitor's prompt", (p) => painted(p) && white(p) > 500);

// That key was the gesture: the context the page armed is playing now.
for (let i = 0; i < 20 && peek.sound !== "running"; ++i) { await sleep(250); peek = await evaluate("window.__ms()"); }
if (peek.sound !== "running") throw new Error(`a key did not start the sound: ${peek.sound}`);
console.log(`sound after a key: ${peek.sound}`);

// And the drive's recordings, which the page fetches for itself on opening.
for (let i = 0; i < 40 && !peek.driveSounds; ++i) { await sleep(250); peek = await evaluate("window.__ms()"); }
if (!peek.driveSounds) throw new Error("the drive's recordings never loaded");
const textBefore = white(peek);
console.log(`at the prompt: frames ${peek.frames}, colours ${peek.colours}, black ${black(peek)}, white ${textBefore}`);

// The page's typing: DIR lists the disk - more text on the screen.
await evaluate('window.__ms.type("DIR\\r")');
peek = await settle("the DIR listing", (p) => white(p) > textBefore * 2);
console.log(`after DIR: frames ${peek.frames}, white ${white(peek)}`);

// The bug report: the page packs the machine's state, the ROM, the mounted
// image and the picture into one .zip (bugreport.js) - built here, not
// downloaded, so its parts can be counted.
const bug = await evaluate('window.__ms.bugreport("browser check")');
console.log(`bug report: ${bug.zip} bytes, ${bug.entries.map((e) => e.name + " " + e.size).join(", ")}`);
const entry = (name) => bug.entries.find((e) => e.name === name);
if (bug.magic !== "PK") throw new Error("the bug report is not a zip");
if (!(entry("state.ms0515")?.size > 100000)) throw new Error("the bug report carries no snapshot");
if (!(entry("rom.bin")?.size > 0 && entry("screen.png")?.size > 0)) throw new Error("the bug report carries no ROM or picture");
if (entry("disks/test_osa_games.dsk")?.size !== 409600) throw new Error("the bug report carries no mounted image");
if (bug.report.note !== "browser check" || bug.report.mounts.fd[0]?.name !== "test_osa_games.dsk")
  throw new Error("the bug report's json does not describe the machine");

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
if (!(peek.frames > 300 && black(peek) > 150000 && textBefore > 500))
  throw new Error("expected RT-11's monitor text on a black screen");
if (!(white(peek) > textBefore * 2))
  throw new Error("typing DIR did not bring a listing");
const listing = white(peek);

// The saved state outlives the page: save the machine as the listing left
// it, load the page anew (which boots it to a bare screen), restore, and
// expect the listing back.
await evaluate('document.getElementById("save").click()');
await sleep(1500);
console.log(`save: "${await evaluate("window.__ms().status")}"`);
await send("Page.navigate", { url: url + (url.includes("?") ? "&" : "?") + "autostart=1" });
const booted = await settle("the reloaded page", painted);
await evaluate('document.getElementById("restore").click()');
await sleep(1500);
const restored = await evaluate("window.__ms()");
console.log(`after reload white ${white(booted)}, after restore white ${white(restored)}, `
            + `status "${restored.status}"`);
// Pressing something in the interface must not take the keyboard away from
// the machine.  The page used to have to hand the focus back after every
// control, so a control added later that forgot to would swallow whatever was
// typed next.  Give a toolbar control the focus, type, and expect the echo.
await evaluate('document.getElementById("joystick").focus()');
const quiet = white(await evaluate("window.__ms()"));
for (let i = 0; i < 6; ++i)
  for (const [code, key, vk] of [["KeyD", "D", 68], ["KeyI", "I", 73], ["KeyR", "R", 82]]) {
    await send("Input.dispatchKeyEvent",
               { type: "keyDown", code, key, text: key, windowsVirtualKeyCode: vk });
    await sleep(40);
    await send("Input.dispatchKeyEvent", { type: "keyUp", code, key, windowsVirtualKeyCode: vk });
  }
const typed = await settle("the echo of what was typed with a control focused",
                           (p) => white(p) > quiet + 40);
console.log(`typed with the joystick button focused: white ${quiet} -> ${white(typed)}`);

// The commander's compare view (Alt+F3): two files marked on one pane and
// put side by side.  The fixture holds one disk, so the two are marked here
// rather than found by name on the other pane.
await evaluate('document.getElementById("files").click()');
await sleep(500);
const key = async (code, k, vk, modifiers = 0) => {
  await send("Input.dispatchKeyEvent", { type: "keyDown", code, key: k, windowsVirtualKeyCode: vk, modifiers });
  await sleep(60);
  await send("Input.dispatchKeyEvent", { type: "keyUp", code, key: k, windowsVirtualKeyCode: vk, modifiers });
  await sleep(60);
};
await key("Insert", "Insert", 45);                       // mark, move on, mark
await key("ArrowDown", "ArrowDown", 40);
await key("Insert", "Insert", 45);
await key("F3", "F3", 114, 1);                           // 1: Alt
await sleep(700);
const diff = await evaluate(`(() => {
  const rows = document.querySelectorAll(".fm-diff .fm-diff-row");
  const bar = [...document.querySelectorAll(".fm-viewer .fm-bar button .label")].map((b) => b.textContent);
  return { rows: rows.length, marks: document.querySelectorAll(".fm-diff mark").length,
           skips: document.querySelectorAll(".fm-diff-skip").length,
           head: document.querySelector(".fm-diff-head")?.textContent ?? "",
           note: document.querySelector(".fm-diff-note")?.textContent ?? "", bar };
})()`);
console.log(`compare: ${diff.rows} rows, ${diff.marks} marked, ${diff.skips} skipped stretches, head "${diff.head}"`);
if (!(diff.rows > 0)) throw new Error("Alt+F3 drew no compare rows");
if (!(diff.marks > 0)) throw new Error("the compare view marked nothing as differing");
if (!diff.bar.includes("Octal")) throw new Error("two binaries are not compared as bytes: " + diff.bar.join(" "));
if (!/differ/.test(diff.note)) throw new Error("the compare view does not say how much differs: " + diff.note);
// F2 shows what was skipped, and puts it back; F6 walks the differences;
// F7 searches both files at once, the hit marked apart from the differences.
const rowCount = () => evaluate('document.querySelectorAll(".fm-diff .fm-diff-row").length');
const shown = await rowCount();
await key("F2", "F2", 113);
const all = await rowCount();
if (!(all > shown)) throw new Error(`F2 did not show what was skipped: ${shown} rows, then ${all}`);
await key("F2", "F2", 113);
if (await rowCount() !== shown) throw new Error("F2 did not hide the same stretches again");
await key("F6", "F6", 117);
await key("F6", "F6", 117, 1);                           // Alt: the difference before
if (/error/.test(await evaluate("window.__ms().status"))) throw new Error("F6 failed: " + await evaluate("window.__ms().status"));
await key("F7", "F7", 118);
await sleep(400);
if (!await evaluate('!!document.querySelector(".fm-dialog input[type=text]")')) throw new Error("F7 opened no search");
await evaluate('(() => { const i = document.querySelector(".fm-dialog input[type=text]"); i.value = "000 002"; i.focus(); })()');
await key("Enter", "Enter", 13);
await sleep(600);
if (!await evaluate('!!document.querySelector(".fm-diff mark.found")'))
  throw new Error("the search marked nothing: " + await evaluate("window.__ms().status"));
console.log(`search in both files: found "${await evaluate('document.querySelector(".fm-diff mark.found").textContent')}"`);

await key("F3", "F3", 114);                              // the same search again, without being asked
await sleep(500);
if (await evaluate('!!document.querySelector(".fm-dialog:not([hidden]) input[type=text]")'))
  throw new Error("F3 asked for the query again instead of repeating the search");
await key("F1", "F1", 112);                              // octal -> hex
await sleep(400);
const asHex = await evaluate('[...document.querySelectorAll(".fm-viewer .fm-bar button .label")].map((b) => b.textContent).join(" ")');
if (!/Hex/.test(asHex)) throw new Error("F1 did not change the representation: " + asHex);

// A search that finds nothing says so in a dialog with one button, not in
// the line under the panes where the eye is not.
await key("F1", "F1", 112);                              // hex -> text, and the query with it
await sleep(400);
await key("F7", "F7", 118);
await sleep(400);
await evaluate('(() => { const i = document.querySelector(".fm-dialog input[type=text]"); i.value = "ZZQQXX"; i.focus(); })()');
await key("Enter", "Enter", 13);
await sleep(600);
const none = await evaluate('(() => { const d = document.querySelector(".fm-dialog:not([hidden]) .fm-dialog-box"); return d ? d.textContent : ""; })()');
if (!/not in either file/.test(none)) throw new Error("a search that found nothing said: " + (none || "nothing"));
if (await evaluate('!!document.querySelector(".fm-dialog:not([hidden]) input[type=text]")'))
  throw new Error("the nothing-found dialog asks for something");
console.log(`nothing found: "${none.trim()}"`);
await key("Enter", "Enter", 13);                         // OK
await sleep(400);
await key("Escape", "Escape", 27);                       // back to the panes
await sleep(300);
if (await evaluate('document.querySelector(".fm-diff").closest(".fm-viewer").hidden') !== true)
  throw new Error("Esc did not leave the compare view");

// One file marked on each pane: those two, whatever they are named.  The two
// panes hold the one disk here, so marking the same file on both must be
// refused rather than compared with itself.
const heading = () => evaluate('document.querySelector(".fm-diff-head")?.textContent ?? ""');
const upIsOpen = () => evaluate('!document.querySelector(".fm-diff").closest(".fm-viewer").hidden');
await key("Home", "Home", 36);                           // the same keys again, from the top:
await key("Insert", "Insert", 45);                       // Insert toggles, so the two marks
await key("ArrowDown", "ArrowDown", 40);                 // made above come off again
await key("Insert", "Insert", 45);
await key("Home", "Home", 36);
await key("Insert", "Insert", 45);                       // the first file, on the left
await key("Tab", "Tab", 9);
await key("Home", "Home", 36);
await key("Insert", "Insert", 45);                       // the same file, on the right
await key("F3", "F3", 114, 1);
await sleep(500);
if (await upIsOpen()) throw new Error("a file was compared with itself");
if (!/with itself/.test(await evaluate("window.__ms().status")))
  throw new Error("the one file marked twice was refused for the wrong reason: " + await evaluate("window.__ms().status"));
await key("Home", "Home", 36);
await key("Insert", "Insert", 45);                       // that one off, the next one on
await key("Insert", "Insert", 45);
await key("F3", "F3", 114, 1);
await sleep(700);
if (!await upIsOpen()) throw new Error("one file marked on each pane did not compare");
const pair = await heading();
console.log(`one marked on each pane: ${pair}`);
const named = pair.match(/DZ\d: ([A-Z0-9.]+)/g) ?? [];
if (named.length !== 2 || named[0] === named[1])
  throw new Error("the two marked files are not the two compared: " + pair);
await key("Escape", "Escape", 27);
await sleep(300);
await evaluate('document.getElementById("files").click()');

ws.close();
if (!/state restored/.test(restored.status))
  throw new Error("the saved state did not outlive the page: " + restored.status);
if (!(white(restored) > white(booted) * 2 && white(restored) > listing / 2))
  throw new Error("the restored screen does not carry the listing");
console.log("browser check OK");
