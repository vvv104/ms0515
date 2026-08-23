// keys.js — the MS7004 key table and the host-keyboard mapping, ported
// from the SDL front-end (Keymap.cpp): the same positional ЙЦУКЕН map in
// РУС mode, the same character-based symbol remapping so a US-layout
// host keyboard produces the characters on its key caps.
//
// KEYS mirrors ms0515::Key (src/lib/include/ms0515/Emulator.hpp) in order:
// a key's index here is its value there.  The module's ms_key_max() is
// checked against the table's length at start-up so drift fails loudly.

export const KEYS = [
  "None",
  "F1", "F2", "F3", "F4", "F5",
  "F6", "F7", "F8", "F9", "F10",
  "F11", "F12", "F13", "F14",
  "Help", "Perform",
  "F17", "F18", "F19", "F20",
  "LBracePipe", "SemiPlus",
  "Digit1", "Digit2", "Digit3", "Digit4", "Digit5",
  "Digit6", "Digit7", "Digit8", "Digit9", "Digit0",
  "MinusEq", "RBraceLeftUp", "Backspace",
  "Tab", "J", "C", "U", "K", "E", "N", "G",
  "LBracket", "RBracket", "Z", "H", "ColonStar", "Tilde", "Return",
  "Ctrl", "Caps",
  "F",
  "Y", "W", "A", "P", "R", "O", "L", "D",
  "V", "Backslash", "Period", "HardSign",
  "ShiftL", "RusLat", "Q", "Che",
  "S", "M", "I", "T", "X", "B",
  "At", "Comma", "Slash", "Underscore", "ShiftR",
  "Compose", "Space", "Kp0Wide", "KpEnter",
  "Find", "Insert", "Remove", "Select", "Prev", "Next",
  "Up", "Down", "Left", "Right",
  "Pf1", "Pf2", "Pf3", "Pf4",
  "Kp1", "Kp2", "Kp3", "Kp4", "Kp5", "Kp6", "Kp7", "Kp8", "Kp9",
  "KpDot", "KpComma", "KpMinus",
];

export const KEY_ID = Object.fromEntries(KEYS.map((name, i) => [name, i]));

// ── the positional maps (Keymap.cpp sdlToMs7004) ───────────────────────────
// KeyboardEvent.code names stand in for SDL scancodes.

// РУС: host position -> the MS7004 key with the Russian letter there.
const RUS = {
  KeyQ: "J", KeyW: "C", KeyE: "U", KeyR: "K", KeyT: "E", KeyY: "N", KeyU: "G",
  KeyI: "LBracket", KeyO: "RBracket", KeyP: "Z", BracketLeft: "H", BracketRight: "HardSign",
  KeyA: "F", KeyS: "Y", KeyD: "W", KeyF: "A", KeyG: "P", KeyH: "R", KeyJ: "O",
  KeyK: "L", KeyL: "D", Semicolon: "V", Quote: "Backslash",
  KeyZ: "Q", KeyX: "Che", KeyC: "S", KeyV: "M", KeyB: "I", KeyN: "T", KeyM: "X",
  Comma: "B", Period: "At",
};

// ЛАТ: host letter -> the MS7004 key with the same Latin letter.
const LAT = {
  BracketLeft: "LBracket", BracketRight: "RBracket", Semicolon: "SemiPlus",
  Quote: "Tilde", Comma: "Comma", Period: "Period",
};
for (const c of "ABCDEFGHIJKLMNOPQRSTUVWXYZ") LAT["Key" + c] = c;

// Both modes.
const COMMON = {
  ShiftLeft: "ShiftL", ShiftRight: "ShiftR", ControlLeft: "Ctrl", CapsLock: "Caps",
  AltLeft: "Compose", AltRight: "RusLat",
  Digit1: "Digit1", Digit2: "Digit2", Digit3: "Digit3", Digit4: "Digit4", Digit5: "Digit5",
  Digit6: "Digit6", Digit7: "Digit7", Digit8: "Digit8", Digit9: "Digit9", Digit0: "Digit0",
  Backquote: "LBracePipe", Minus: "MinusEq", Equal: "RBraceLeftUp",
  Backslash: "Backslash", Slash: "Slash",
  Space: "Space", Enter: "Return", Tab: "Tab", Backspace: "Backspace",
  Home: "Find", Insert: "Insert", Delete: "Remove", End: "Select",
  PageUp: "Prev", PageDown: "Next",
  ArrowLeft: "Left", ArrowRight: "Right", ArrowUp: "Up", ArrowDown: "Down",
  F1: "F1", F2: "F2", F3: "F3", F4: "F4", F5: "F5", F6: "F6", F7: "F7",
  F8: "F8", F9: "F9", F10: "F10", F11: "F11", F12: "F12", F13: "F13",
  NumLock: "Pf1",
  Numpad0: "Kp0Wide", Numpad1: "Kp1", Numpad2: "Kp2", Numpad3: "Kp3", Numpad4: "Kp4",
  Numpad5: "Kp5", Numpad6: "Kp6", Numpad7: "Kp7", Numpad8: "Kp8", Numpad9: "Kp9",
  NumpadDecimal: "KpDot", NumpadEnter: "KpEnter", NumpadSubtract: "KpMinus",
  NumpadComma: "KpComma",
};

function positional(code, rus) {
  return (rus ? RUS[code] : LAT[code]) ?? COMMON[code] ?? null;
}

// ── the character-based map (Keymap.cpp sdlToMs7004Char) ───────────────────
// (code, host Shift held, РУС) -> { key, withShift }: the MS7004 key and
// whether it needs Shift there to make the character on the host's cap.
const RUS_SHIFTED = {
  Slash: ["Comma", false], Backslash: ["Slash", false], Backquote: [null, false],
  Digit4: ["SemiPlus", false], Digit6: ["ColonStar", false], Digit7: ["Slash", true],
  Digit8: ["ColonStar", true], Digit9: ["Digit8", true], Digit0: ["Digit9", true],
  Minus: ["Underscore", false], Equal: ["SemiPlus", true],
};
const RUS_PLAIN = {
  Slash: ["Period", false], Backquote: [null, false], Equal: ["MinusEq", true],
};
const LAT_SHIFTED = {
  Backquote: ["Tilde", false], Digit2: ["At", false], Digit6: ["Che", false],
  Minus: ["Underscore", false], Semicolon: ["ColonStar", false],
  BracketLeft: ["LBracePipe", false], BracketRight: ["RBraceLeftUp", false],
  Digit7: ["Digit6", true], Digit8: ["ColonStar", true], Digit9: ["Digit8", true],
  Digit0: ["Digit9", true], Equal: ["SemiPlus", true], Quote: ["Digit2", true],
  Backslash: ["LBracePipe", true],
};
const LAT_PLAIN = {
  Backquote: ["Digit7", true], Equal: ["MinusEq", true], Quote: ["Digit7", true],
};

export function mapKey(code, shifted, rus) {
  const table = rus ? (shifted ? RUS_SHIFTED : RUS_PLAIN) : (shifted ? LAT_SHIFTED : LAT_PLAIN);
  const special = table[code];
  if (special) return { key: special[0], withShift: special[1] };
  return { key: positional(code, rus), withShift: shifted };
}

// Letter keys (Emulator's isLetterKey): the CAPS + Shift inversion applies.
const LAT_LETTERS = new Set([..."ABCDEFGHIJKLMNOPQRSTUVWXYZ"]);
const RUS_LETTERS = new Set([...LAT_LETTERS, "LBracket", "RBracket", "Backslash", "HardSign",
                             "Che", "At", "ColonStar", "Tilde"]);
export function isLetterKey(name, rus) {
  return (rus ? RUS_LETTERS : LAT_LETTERS).has(name);
}

// ── typing: a character -> the host key event that makes it ────────────────
// For the `type=` URL parameter and the "Type" box: ASCII only, the ЛАТ map.
const CHAR_CODES = { " ": "Space", "\n": "Enter", "\r": "Enter", "\t": "Tab",
  "-": "Minus", "=": "Equal", "[": "BracketLeft", "]": "BracketRight", ";": "Semicolon",
  "'": "Quote", ",": "Comma", ".": "Period", "/": "Slash", "\\": "Backslash", "`": "Backquote" };
const SHIFTED_CHARS = { "~": "Backquote", "!": "Digit1", "@": "Digit2", "#": "Digit3",
  "$": "Digit4", "%": "Digit5", "^": "Digit6", "&": "Digit7", "*": "Digit8", "(": "Digit9",
  ")": "Digit0", "_": "Minus", "+": "Equal", "{": "BracketLeft", "}": "BracketRight",
  ":": "Semicolon", "\"": "Quote", "<": "Comma", ">": "Period", "?": "Slash", "|": "Backslash" };
export function charToHostKey(ch) {
  if (/[A-Za-z]/.test(ch)) return { code: "Key" + ch.toUpperCase(), shift: ch === ch.toUpperCase() };
  if (/[0-9]/.test(ch)) return { code: "Digit" + ch, shift: false };
  if (CHAR_CODES[ch]) return { code: CHAR_CODES[ch], shift: false };
  if (SHIFTED_CHARS[ch]) return { code: SHIFTED_CHARS[ch], shift: true };
  return null;
}
