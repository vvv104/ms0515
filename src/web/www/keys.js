// keys.js — the MS7004 key table and the browser key mapping.
//
// KEYS mirrors ms0515::Key (src/lib/include/ms0515/Emulator.hpp) in order:
// a key's index here is its value there.  The module's ms_key_max() is
// checked against the table's length at start-up so drift fails loudly.
//
// CODE_TO_KEY maps KeyboardEvent.code (the physical key) the way the SDL
// front-end's Keymap.cpp maps scancodes in its Latin mode: host letters to
// the MS7004 keys with the same Latin letters, the digits, the editing
// cluster, the arrows, the keypad, the function keys.

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

const latin = {};
for (const c of "ABCDEFGHIJKLMNOPQRSTUVWXYZ") latin["Key" + c] = c;
for (let d = 0; d <= 9; ++d) latin["Digit" + d] = "Digit" + d;

export const CODE_TO_KEY = {
  ...latin,
  BracketLeft: "LBracket", BracketRight: "RBracket",
  Semicolon: "SemiPlus", Quote: "Tilde", Comma: "Comma", Period: "Period",
  ShiftLeft: "ShiftL", ShiftRight: "ShiftR", ControlLeft: "Ctrl",
  CapsLock: "Caps", AltLeft: "Compose", AltRight: "RusLat",
  Backquote: "LBracePipe", Minus: "MinusEq", Equal: "RBraceLeftUp",
  Backslash: "Backslash", Slash: "Slash",
  Space: "Space", Enter: "Return", Tab: "Tab", Backspace: "Backspace",
  Home: "Find", Insert: "Insert", Delete: "Remove", End: "Select",
  PageUp: "Prev", PageDown: "Next",
  ArrowLeft: "Left", ArrowRight: "Right", ArrowUp: "Up", ArrowDown: "Down",
  F1: "F1", F2: "F2", F3: "F3", F4: "F4", F5: "F5", F6: "F6", F7: "F7",
  F8: "F8", F9: "F9", F10: "F10", F11: "F11", F12: "F12",
  Numpad1: "Kp1", Numpad2: "Kp2", Numpad3: "Kp3", Numpad4: "Kp4", Numpad5: "Kp5",
  Numpad6: "Kp6", Numpad7: "Kp7", Numpad8: "Kp8", Numpad9: "Kp9",
  Numpad0: "Kp0Wide", NumpadDecimal: "KpDot", NumpadSubtract: "KpMinus",
  NumpadEnter: "KpEnter", NumpadAdd: "KpComma",
  NumLock: "Pf1", NumpadDivide: "Pf2", NumpadMultiply: "Pf3",
};
