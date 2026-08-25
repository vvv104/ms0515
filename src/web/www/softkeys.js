// softkeys.js — the OS's on-screen keyboard as the machine's, on a touch
// device.
//
// A phone or a tablet raises its keyboard only for a focused text field,
// and only on a tap: the ⌨ button focuses a field nobody sees, and what the
// keyboard produces arrives here as input events (insertText with the
// characters, deleteContentBackward, insertLineBreak) rather than key
// codes - each character goes to the machine through the page's typing
// (`type`), Cyrillic letters as the РУС positions.  A hardware keyboard
// attached to the tablet still sends key events with codes; those go the
// usual way (`onKey`).
const CYRILLIC = {          // the ЙЦУКЕН positions of the MS7004 (keys.js RUS)
  й: "KeyQ", ц: "KeyW", у: "KeyE", к: "KeyR", е: "KeyT", н: "KeyY", г: "KeyU",
  ш: "KeyI", щ: "KeyO", з: "KeyP", х: "BracketLeft", ъ: "BracketRight",
  ф: "KeyA", ы: "KeyS", в: "KeyD", а: "KeyF", п: "KeyG", р: "KeyH", о: "KeyJ",
  л: "KeyK", д: "KeyL", ж: "Semicolon", э: "Quote",
  я: "KeyZ", ч: "KeyX", с: "KeyC", м: "KeyV", и: "KeyB", т: "KeyN", ь: "KeyM",
  б: "Comma", ю: "Period",
};

export const isTouchDevice = () => navigator.maxTouchPoints > 1 || matchMedia("(pointer: coarse)").matches;

// A character from the on-screen keyboard -> { code, shift, rus } for the
// typing queue (rus: the mode the machine must be in), or null.
export function softChar(ch, latin) {
  const lower = ch.toLowerCase();
  if (CYRILLIC[lower]) return { code: CYRILLIC[lower], shift: ch !== lower, rus: true };
  const k = latin(ch);
  if (!k) return null;
  return /[A-Za-z]/.test(ch) ? { ...k, rus: false } : k;
}

export class SoftKeyboard {
  // `field` is the hidden input; `type(items)` queues typing items;
  // `onKey(e, down)` takes a real key event (a hardware keyboard).
  constructor(field, type, onKey, latin) {
    this.field = field;
    this.type = type;
    this.latin = latin;
    field.addEventListener("beforeinput", (e) => this.beforeInput(e));
    field.addEventListener("input", () => { field.value = ""; });
    field.addEventListener("keydown", (e) => {
      // A code names a hardware key: the keyboard path.  The soft keyboard's
      // keys come without one (or "Unidentified") and go through beforeinput.
      if (e.code && e.code !== "Unidentified") { onKey(e, true); return; }
      if (e.key === "Enter") { this.type([{ code: "Enter" }]); e.preventDefault(); }
      else if (e.key === "Backspace") { this.type([{ code: "Backspace" }]); e.preventDefault(); }
    });
    field.addEventListener("keyup", (e) => { if (e.code && e.code !== "Unidentified") onKey(e, false); });
  }

  get open() { return document.activeElement === this.field; }

  // Must run inside the tap's handler: the OS raises the keyboard only then.
  toggle() {
    if (this.open) this.field.blur(); else this.field.focus();
  }

  beforeInput(e) {
    const t = e.inputType;
    if (t === "insertText" || t === "insertCompositionText" || t === "insertFromPaste") {
      const items = [];
      for (const ch of e.data ?? "") {
        if (ch === "\n") { items.push({ code: "Enter" }); continue; }
        const k = softChar(ch, this.latin);
        if (k) items.push(k);
      }
      this.type(items);
    } else if (t === "insertLineBreak" || t === "insertParagraph") {
      this.type([{ code: "Enter" }]);
    } else if (t === "deleteContentBackward") {
      this.type([{ code: "Backspace" }]);
    }
    e.preventDefault();
  }
}
