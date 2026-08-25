// joystick.js — the joystick on the MS7007 port, from the host's keys or a
// touch screen.
//
// The machine's joystick is five switches - right, left, down, up, fire
// (the Kempston order, bits 0-4) - on port B of the second PPI; SABOT2's
// "J KEMPSTON" option reads it.  Here the lines come from the arrow keys
// and Space while the joystick is on (those keys then do not reach the
// MS7004), and on a touch screen from an overlay: a floating stick under
// the first finger on the left part of the screen (the direction is the
// finger's offset from where it landed, eight ways), a fire button under
// a finger on the right part.
export const JOY = { RIGHT: 1, LEFT: 2, DOWN: 4, UP: 8, FIRE: 16 };

const KEY_LINES = { ArrowRight: JOY.RIGHT, ArrowLeft: JOY.LEFT, ArrowDown: JOY.DOWN, ArrowUp: JOY.UP, Space: JOY.FIRE };
const DEAD_ZONE = 14;            // px: the stick's centre where no direction is held

export class Joystick {
  // `apply(bits)` hands the lines to the machine; `overlay` is the element
  // over the screen that takes the touches (shown only while enabled).
  constructor(apply, overlay) {
    this.apply = apply;
    this.overlay = overlay;
    this.enabled = false;
    this.keyBits = 0;
    this.touchBits = 0;
    this.stick = null;             // { id, x0, y0 } while a finger holds the stick
    this.fireId = null;
    overlay.addEventListener("pointerdown", (e) => this.down(e));
    overlay.addEventListener("pointermove", (e) => this.move(e));
    for (const t of ["pointerup", "pointercancel"]) overlay.addEventListener(t, (e) => this.up(e));
    overlay.addEventListener("contextmenu", (e) => e.preventDefault());
  }

  enable(on) {
    this.enabled = on;
    this.keyBits = this.touchBits = 0;
    this.stick = null; this.fireId = null;
    this.overlay.hidden = !on || !("ontouchstart" in window || navigator.maxTouchPoints > 0);
    this.push();
  }

  push() { this.apply(this.enabled ? (this.keyBits | this.touchBits) : 0); }

  // A key event; true when the joystick took it.
  key(code, down) {
    if (!this.enabled) return false;
    const line = KEY_LINES[code];
    if (!line) return false;
    if (down) this.keyBits |= line; else this.keyBits &= ~line;
    this.push();
    return true;
  }

  down(e) {
    this.overlay.setPointerCapture(e.pointerId);
    const r = this.overlay.getBoundingClientRect();
    const left = e.clientX - r.left < r.width * 0.55;
    if (left && !this.stick) {
      this.stick = { id: e.pointerId, x0: e.clientX, y0: e.clientY };
      this.showStick(e.clientX - r.left, e.clientY - r.top, true);
    } else if (!left && this.fireId === null) {
      this.fireId = e.pointerId;
      this.touchBits |= JOY.FIRE;
      this.overlay.classList.add("fire");
      this.push();
    }
    e.preventDefault();
  }

  move(e) {
    if (!this.stick || e.pointerId !== this.stick.id) return;
    const dx = e.clientX - this.stick.x0, dy = e.clientY - this.stick.y0;
    let bits = 0;
    if (Math.hypot(dx, dy) > DEAD_ZONE) {
      const a = Math.atan2(-dy, dx) * 180 / Math.PI;      // 0 = right, 90 = up
      if (a > -67.5 && a < 67.5) bits |= JOY.RIGHT;
      if (a > 112.5 || a < -112.5) bits |= JOY.LEFT;
      if (a > 22.5 && a < 157.5) bits |= JOY.UP;
      if (a < -22.5 && a > -157.5) bits |= JOY.DOWN;
    }
    this.touchBits = (this.touchBits & JOY.FIRE) | bits;
    const r = this.overlay.getBoundingClientRect();
    this.showKnob(e.clientX - r.left, e.clientY - r.top);
    this.push();
    e.preventDefault();
  }

  up(e) {
    if (this.stick && e.pointerId === this.stick.id) {
      this.stick = null;
      this.touchBits &= JOY.FIRE;
      this.showStick(0, 0, false);
    } else if (e.pointerId === this.fireId) {
      this.fireId = null;
      this.touchBits &= ~JOY.FIRE;
      this.overlay.classList.remove("fire");
    }
    this.push();
  }

  showStick(x, y, on) {
    const base = this.overlay.querySelector(".stick");
    base.hidden = !on;
    base.style.left = x + "px"; base.style.top = y + "px";
    this.showKnob(x, y);
  }

  showKnob(x, y) {
    if (!this.stick) return;
    const r = this.overlay.getBoundingClientRect();
    const x0 = this.stick.x0 - r.left, y0 = this.stick.y0 - r.top;
    const dx = x - x0, dy = y - y0, d = Math.hypot(dx, dy), max = 40;
    const k = d > max ? max / d : 1;
    const knob = this.overlay.querySelector(".knob");
    knob.style.transform = `translate(${dx * k}px, ${dy * k}px)`;
  }
}
