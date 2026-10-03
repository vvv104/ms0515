// keys_check.mjs — www/keys.js under Node: the keys a PC has no cap for.
// The right Shift with an F-key is one of them (shiftedFunctionKey), the
// same table as the lib's (src/lib/tests/test_keyboard_layout.cpp): the
// machine's key is ten further on, and F13, F14 are on F11, F12.
//
//   node src/web/keys_check.mjs
import { KEY_ID, shiftedFunctionKey } from "./www/keys.js";

let checks = 0;
function same(got, want, what) {
  ++checks;
  if (got !== want) throw new Error(`${what}: ${got}, not ${want}`);
}

const want = { F1: "Pf1", F2: "Pf2", F3: "Pf3", F4: "Pf4", F5: "Help", F6: "Perform",
               F7: "F17", F8: "F18", F9: "F19", F10: "F20", F11: "F13", F12: "F14" };
for (const [code, key] of Object.entries(want)) {
  same(shiftedFunctionKey(code), key, `Shift+${code}`);
  ++checks;
  if (!(key in KEY_ID)) throw new Error(`${key} is not a key of the machine`);
}
for (const code of ["F13", "F0", "F", "KeyF", "Digit1", "ShiftRight"])
  same(shiftedFunctionKey(code), null, code);

console.log(`keys check: ${checks} checks OK`);
