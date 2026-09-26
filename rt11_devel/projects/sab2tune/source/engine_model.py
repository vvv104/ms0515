"""A behavioural model of the Saboteur II 48K title-tune engine (0xfb00..0xffff).

The engine is self-modifying Z80 code; this is the same machine written
with named variables, T-state accounting per path, and the 50 Hz interrupt
taken where it falls.  It reproduces the Spectrum's speaker output to the
T-state (checked against a Z80 emulator running the original: every one
of the 66637 level changes of the tune), and is the specification the
MACRO-11 engine follows - each Spectrum address quoted there is a path
here.

Each level change carries a tag naming the paths that led to it: the
tail of the previous half period (A no step while the effect is delayed,
K no step by the counter, u/d a step up/down, U/D a step turned at its
limit, S a step stopped there, - the note's start), the head (D the duty
drifted, n not) and the phase (H high, L low, C the duty caught up).
The port's tests use the tags to find which path a timing error is in.

Usage: engine_model.py <code block at 0x620C> <Spectrum48.rom> <out>
    writes "T level tag" lines; with a fourth argument, a toggle log with
    F lines, the interrupt is taken at those times instead of the frame
    boundaries (to compare with a run whose acceptance is instruction-exact).
"""
import sys

FRAME_T = 69888
BASE = 0x620C


class NoteOver(Exception):
    """The interrupt ended the note: the tone loop is abandoned (0xfdb8)."""


class Stopped(Exception):
    """The frame budget is spent."""


class Engine:
    def __init__(self, code, rom, max_frames=4000, accept_times=None, frame_t=FRAME_T, first_tick=None):
        self.accept_times = accept_times   # the emulator's per-frame accept T, or None
        self.frame_t = frame_t             # the frame, in T-states (the machine's may differ)
        self.first_tick = first_tick       # when the first one comes, if not a whole frame in
        self.mem = code
        self.noise = rom[0x1000:0x1000 + 20]
        self.max_frames = max_frames
        self.t = 0                 # T-states since entry
        self.frames = 0
        self.out = []              # (T, level 0/1)
        self.iff = False           # interrupts enabled
        self.pending = False       # a frame tick arrived while disabled
        self.events = []           # (T, text) for the listing
        self.marks = []            # (name, T, ...) checkpoints
        self.tail = '-'            # the tag pieces, see the header
        self.head = ''
        self.phase = ''

    # -- memory helpers -------------------------------------------------------
    def rd(self, a):
        return self.mem[a - BASE]

    def rd16(self, a):
        return self.rd(a) | (self.rd(a + 1) << 8)

    # -- time -----------------------------------------------------------------
    def boundary(self):
        k = self.frames
        if self.accept_times is not None and k < len(self.accept_times):
            return self.accept_times[k]
        if self.first_tick is not None:
            return self.first_tick + k * self.frame_t
        return (k + 1) * self.frame_t

    def tick(self, n):
        """Spend n T-states.  A frame boundary inside them takes the
        interrupt right there (when enabled) and the rest follows it; with
        interrupts disabled the tick is lost, as the ULA's pulse is."""
        if n <= 0:                         # a correction of a path cost
            self.t += n
            return
        while n > 0:
            b = self.boundary()
            if self.t >= b:                # already past it: taken now
                self.frames += 1
                if self.iff:
                    self.interrupt()
                continue
            if self.t + n < b:
                self.t += n
                return
            step = b - self.t
            self.t = b
            n -= step
            self.frames += 1
            if self.iff:
                self.interrupt()           # may raise NoteOver

    def out_level(self, level, tag=''):
        self.out.append((self.t, level, tag))

    def mark(self, name, *info):
        self.marks.append((name, self.t) + info)

    # -- entry (0xfb10) --------------------------------------------------------
    def run(self):
        try:
            self.start()
        except Stopped:
            pass

    def start(self):
        # percussion: the record list at 0xff45
        self.perc_next = 0xff45
        self.perc_ptr, self.perc_cnt = self.load_perc_record()
        self.b_alt = 4
        self.c_alt = 0
        self.tick(296)             # DI .. CALL 0xfb3a .. JR 0xfb46
        # engine variables (the self-modified immediates)
        self.transpose = 0
        self.mode = 0
        self.instr = 0xfe3c
        self.shift = 0
        self.dmask = 0
        self.dir_init = 0x13
        self.loop_cnt = 0
        self.resume = 0
        self.note_rep = 1
        self.pitch_hi = 0
        self.at_limit = 0          # the JR displacement at 0xfd0b: 0x14 reverse, 0 stop
        self.lim_lo = 0
        self.lim_hi = 0
        self.echo_rem = 0
        self.jr2 = 0x20            # the JR opcode at 0xfcd9: 0x20 JR nz, 0x18 JR always
        self.step = 0xffff
        self.smask = 0
        self.period = 0
        self.duty = 0
        self.pass_cnt = 0
        self.dir = 0x13
        self.level = 0             # the byte at 0xfcc6 (0 or 0x10)
        self.scnt = 0
        self.drum_b = 1
        self.drum_rra = 0
        self.a_alt = 0             # the duration counter (A')
        self.stack = []
        self.stopped = False
        self.hl = 0xff0e
        self.iff = False
        self.interpret()

    def load_perc_record(self):
        p = self.perc_next
        ptr = self.rd16(p)
        cnt = self.rd(p + 2)
        self.perc_next = p + 3
        return ptr, cnt

    # -- the script interpreter (0xfb46) --------------------------------------
    def interpret(self):
        while True:
            if self.frames >= self.max_frames:
                return
            self.mark('fb46')
            a = self.rd(self.hl)
            self.hl += 1
            self.tick(7 + 6 + 4 + 10)          # LD A,(HL); INC HL; OR A; JP p
            if a < 0x80:
                self.note_rep = a
                self.tick(13)                  # LD (0xfdbd),A
                if not self.play_note():
                    return
                continue
            a &= 0x7f
            self.tick(7 + 10)                  # AND 7f; JP z
            if a == 0:
                self.events.append((self.t, 'end'))
                return
            # the DEC A / JR nz chain: 16 per command skipped, 11 to land
            if a >= 7:
                self.tick(6 * 16)
                n = self.rd(self.hl)           # slide parameters
                self.hl += 1
                q = 0xfe3c + n
                self.shift = self.rd(q)
                self.dmask = self.rd(q + 1)
                self.dir_init = self.rd(q + 2)
                self.tick(7 + 6 + 7 + 4 + 7 + 12 + 7 + 13 + 6 + 7 + 13 + 6 + 7 + 13 + 12)
                continue
            self.tick(16 * (a - 1) + 11)
            if a == 1:                         # call
                de = self.rd16(self.hl)
                self.hl += 2
                self.stack.append(self.hl)
                self.hl = de
                self.tick(7 + 6 + 7 + 6 + 11 + 4 + 12)
            elif a == 2:                       # return
                self.hl = self.stack.pop()
                self.tick(10 + 12)
            elif a == 3:                       # loop
                de = self.rd16(self.hl)
                b = self.rd(self.hl + 2)
                self.hl += 3
                cnt = self.loop_cnt
                self.tick(7 + 6 + 7 + 6 + 7 + 6 + 7 + 4)
                if cnt == 0:
                    cnt = b
                    self.tick(7 + 4)
                else:
                    self.tick(12)
                cnt = (cnt - 1) & 0xff
                self.loop_cnt = cnt
                self.tick(4 + 13)
                if cnt != 0:
                    self.hl = de
                    self.tick(7 + 4 + 12)
                else:
                    self.tick(12)
            elif a == 4:                       # transpose
                self.transpose = self.rd(self.hl)
                self.hl += 1
                self.tick(7 + 6 + 13 + 12)
            elif a == 5:                       # instrument, mode 1
                self.tick(7)
                self.set_instrument(1)
            else:                              # instrument, mode 2
                self.tick(7 + 12)
                self.set_instrument(2)

    def set_instrument(self, mode):
        n = self.rd(self.hl)
        self.hl += 1
        self.tick(7 + 6 + 4)
        if n == 0:
            self.mode = 0
            self.tick(12 + 13 + 12)
        else:
            self.instr = 0xfe3c + n
            self.mode = mode
            self.tick(7 + 7 + 4 + 7 + 12 + 20 + 4 + 13 + 12)

    # -- a note record: note_rep pitch bytes (0xfbc4) -------------------------
    def play_note(self):
        while True:
            self.mark('fbc4')
            c = self.rd(self.hl)
            self.hl += 1
            self.resume = self.hl
            self.pitch_hi = c & 0xf8
            self.tick(7 + 6 + 16 + 4 + 7 + 13 + 4)
            hl, dur, rest = self.pitch(c)
            self.tick(10)                      # JP z
            if rest:
                raise NotImplementedError('rest')
            self.a_alt = dur
            self.tick(4)                       # EX AF,AF'
            self.setup_effects(hl)
            try:
                self.tone_loop()
            except NoteOver:
                pass
            # the note ended: the interrupt discarded the tone loop (0xfdb8)
            self.note_rep = (self.note_rep - 1) & 0xff
            self.tick(16 + 7 + 4 + 13 + 10)    # LD HL,(nn); LD A,n; DEC A; LD (nn),A; JP z
            if self.note_rep == 0:
                return True
            self.tick(10)

    def pitch(self, c):
        """0xfdc8 (with its CALL): the period and the duration of pitch byte c."""
        a = ((c >> 3) & 0x1f)
        a = (a + self.transpose) & 0xff
        b = -1
        while True:
            b += 1
            a -= 12
            if a < 0:
                break
        a += 12
        t = 17 + 4 + 12 + 7 + 7 + 7 + 23 * b + 18
        hl = self.rd16(0xfe24 + 2 * a)
        t += 7 + 4 + 10 + 17 + 37 + 6 + 7 + 4 + 4 + 4
        if b != 0:
            t += 7 + 29 * (b - 1) + 24
            hl >>= b
        else:
            t += 12
        hl = (hl - 0x46) & 0xffff
        t += 10 + 4 + 15 + 4 + 7 + 4 + 10 + 17 + 37 + 4 + 4 + 4 + 7 + 4 + 10
        dur = self.rd(0xff3d + (c & 7))
        self.tick(t)
        return hl, dur, (c & 0xf8) == 0

    def read_instrument(self):
        """0xfe04 (with its CALL): (0xfcd8) = byte 0, the step = byte 1
        sign-extended, A = byte 2; the pointer to byte 2 comes back in DE."""
        p = self.instr
        self.smask = self.rd(p)
        c = self.rd(p + 1)
        self.step = c | (0xff00 if c & 0x80 else 0)
        a = self.rd(p + 2)
        self.tick(17 + 4 + 10 + 7 + 13 + 6 + 7 + 7 + 6 + 8 + (7 + 4 if c & 0x80 else 12) + 20 + 7 + 4 + 10)
        return a, p + 2

    def setup_effects(self, hl):
        """0xfbd7..0xfc6d: the mode's setup, the delayed effect, the duty offset."""
        mode = self.mode
        self.mark('fbd7')
        de = None
        if mode & 1:                           # vibrato around the note
            self.at_limit = 0x14
            self.tick(7 + 4 + 7 + 7 + 13)
            a, de = self.read_instrument()
            self.lim_lo = (hl - a) & 0xffff
            self.lim_hi = (hl + a) & 0xffff
            self.tick(4 + 7 + 4 + 15 + 16 + 11 + 11 + 16 + 15)
        elif mode & 2:                         # slide to a fixed target
            self.at_limit = 0
            self.tick(7 + 4 + 12 + 4 + 7 + 4 + 13)
            a, de = self.read_instrument()
            self.tick(11 + 11 + 8 + (12 if self.step & 0x8000 else 7 + 8) + 7 + 7)
            lim, _, _ = self.pitch(0x90)
            self.lim_lo = lim
            self.lim_hi = lim
            self.tick(16 + 16 + 10 + 10 + 12)
        else:                                  # plain
            self.lim_hi = hl
            self.lim_lo = hl
            self.step = 0
            self.at_limit = 0
            self.tick(7 + 4 + 12 + 4 + 12 + 16 + 16 + 10 + 20 + 4 + 13 + 16)
        # 0xfbf4: instrument byte 3 delays the effect by that many frames
        if de is not None:
            b = self.rd(de + 1)
            self.tick(6 + 7 + 4)
            if b != 0:
                a = self.a_alt
                self.tick(7 + 4 + 4 + 4)
                if a > b:
                    self.echo_rem = a - b
                    self.a_alt = b
                    self.jr2 = 0x18
                    self.tick(12 + 7 + 4 + 4 + 4 + 4 + 13 + 7 + 13 + 12)
                elif a == b:
                    self.tick(12 + 12 + 4 + 4 + 12)
                else:
                    self.tick(7 + 4 + 4 + 12)
            else:
                self.tick(12)
        self.period = hl
        self.mark('fc49')
        self.tick(16)
        # 0xfc4c: the duty offset: DE = HL - 2, then (DE + 2) >> 1 for each
        # of the shift - 1 remaining turns of the DJNZ; 0 when the shift is 0
        de = 0
        self.tick(10 + 7 + 4)
        if self.shift != 0:
            de = (hl - 2) & 0xffff
            b = (self.shift - 1) & 0xff
            self.tick(7 + 4 + 4 + 4 + 6 + 6 + 4)
            if b != 0:
                self.tick(7)
                for _ in range(b):
                    de = ((de + 2) & 0xffff) >> 1
                    self.tick(6 + 6 + 8 + 8 + 13)
                self.tick(-5)
            else:
                self.tick(12)
        else:
            self.tick(12)
        self.duty = de
        self.dir = self.dir_init
        self.tail = '-'
        self.tick(20 + 7 + 13 + 4)
        self.iff = True

    # -- the tone loop (0xfc6e) ------------------------------------------------
    def tone_loop(self):
        """Half periods until the interrupt ends the note.  Returns False when
        the frame budget is exhausted."""
        while True:
            if self.frames >= self.max_frames:
                raise Stopped()
            # 0xfc6e: the duty offset drifts every (dmask + 1) passes
            self.mark('fc6e')
            de = self.duty
            self.pass_cnt = (self.pass_cnt + 1) & 0xff
            self.tick(10 + 7 + 4 + 13 + 7)
            self.head = 'n'
            if (self.pass_cnt & self.dmask) == 0:
                # the drift instruction at 0xfc7b: INC DE, DEC DE or a NOP
                self.head = 'D'
                self.tick(7 + (4 if self.dir == 0 else 6))
                if self.dir == 0x13:
                    de = (de + 1) & 0xffff
                elif self.dir == 0x1b:
                    de = (de - 1) & 0xffff
            else:
                self.tick(12)
            hl = self.period
            self.tick(13 + 8)
            if self.level & 0x10:
                hl = (hl + de) & 0xffff
                self.phase = 'H'
                self.tick(7 + 11 + 20 + 12 + 68)
            else:
                self.tick(12 + 4 + 6 + 15 + 6)
                if hl > de:
                    hl = (hl - de) & 0xffff
                    self.phase = 'L'
                    self.tick(12 + 68)
                else:
                    hl = 1
                    self.phase = 'C'
                    self.dir ^= 8
                    if self.dir == 0x13:
                        de = (de + 1) & 0xffff
                    elif self.dir == 0x1b:
                        de = (de - 1) & 0xffff
                    self.tick(7 + 10 + 13 + 7 + 13 + 13 + (4 if self.dir == 8 else 6) + 12)
            self.duty = de
            # 0xfcb4: the delay, 2048 per H and 16 per L/2
            h = (hl >> 8) & 0xff
            l = hl & 0xff
            h1 = (h + 1) & 0xff
            outer = (h1 - 1) & 0xff
            delay = 16 * ((l >> 1) + 1) - 5 + 21 + outer * 2048
            self.mark('fcb4', hl, de)
            self.tick(20 + 4 + 8 + 4)
            self.tick(delay)
            # 0xfcc5: the toggle
            self.level ^= 0x10
            self.tick(7 + 7 + 13)
            self.mark('fccc')
            # the OUT completes before an interrupt can be taken: log it
            # first (the emulator reports it 10 T into the instruction)
            self.out.append((self.t + 10, 1 if self.level else 0, self.tail + self.head + self.phase))
            self.tick(11)
            # 0xfcce: the slide step every (smask + 1) half periods
            hl = self.period
            self.scnt = (self.scnt - 1) & 0xff
            self.tick(10 + 7 + 4 + 13 + 7)
            # (0xfcd9) is the JR's opcode: 0x20 = JR nz (the step every
            # smask+1 half periods), 0x18 = JR always (no step: the first
            # part of a note whose instrument delays the effect)
            if self.jr2 == 0x18 or (self.scnt & self.smask) != 0:
                self.tail = 'A' if self.jr2 == 0x18 else 'K'
                self.tick(12 + 7 + 203 + 10)   # pad 16
            else:
                self.tick(7)
                self.slide_step(hl, de)

    def slide_step(self, hl, de):
        """0xfcdb: the period moves by the step, bouncing or stopping at the limits."""
        step = self.step
        hl = (hl + step) & 0xffff
        self.tick(10 + 11 + 8 + 4 + 4)
        if step & 0x8000:
            self.tick(12 + 10 + 4 + 15)
            hit = hl < self.lim_lo
            self.tick(12 if hit else 7 + 12)
        else:
            self.tick(7 + 10 + 6 + 4 + 15)
            hit = hl >= self.lim_hi + 1 and ((hl - 1 - self.lim_hi) & 0xffff) < 0x8000
            hit = ((hl - 1) & 0xffff) >= self.lim_hi
            self.tick(12 if hit else 7 + 10)
        if not hit:
            self.period = hl
            self.tail = 'd' if step & 0x8000 else 'u'
            self.tick(7 + 86 + 4 + 16 + 10)
            return
        # 0xfd07: at the limit
        self.tick(10 + 12)
        lim = self.lim_lo if step & 0x8000 else self.lim_hi
        if self.at_limit == 0x14:
            self.step = (-step) & 0xffff
            self.tail = 'D' if step & 0x8000 else 'U'
            self.tick(20 + 4 + 15 + 12)
        else:
            self.step = 0
            self.tail = 'S'
            self.tick(4 * 13)
        self.period = lim
        self.tick(16 + 4 + 4 + 12 + 16 + 10)

    # -- the interrupt ---------------------------------------------------------
    def interrupt(self):
        """0xfd34 via the IM 2 vector.  Raises NoteOver when the note is over
        (the tone loop is abandoned)."""
        if self.frames >= self.max_frames:
            raise Stopped()
        self.iff = False
        self.tick(19 + 12 + 10 + 4 + 4)
        self.b_alt = (self.b_alt - 1) & 0xff
        if self.b_alt != 0:
            self.tick(13 + 4)
            self.count_duration()
            return
        self.tick(8 + 11 + 4 + 11 + 4 + 7 + 12)      # no key
        self.c_alt += 1
        self.tick(4 + 4 + 7)
        if self.c_alt == 16:
            self.tick(7 + 7 + 4)
            a = (self.perc_cnt - 1) & 0xff
            if a == 0:
                self.perc_ptr, a = self.load_perc_record()
                self.tick(17 + 10 + 7 + 6 + 7 + 6 + 7 + 6 + 16 + 20 + 10)
            else:
                self.tick(10)
            self.perc_cnt = a
            self.c_alt = 0
            self.tick(13 + 4 + 4)
        else:
            self.tick(12)
        drum = self.rd(self.perc_ptr + self.c_alt)
        self.tick(10 + 17 + 4 + 4 + 12 + 7 + 10 + 4)
        if drum != 0:
            self.tick(7)                       # JR z not taken
            # 0xfd60: type 1 keeps the ROM byte whole (a NOP in the loop);
            # a type k > 1 shifts it right k - 1 times (RRA) - a shorter click
            self.tick(7 + 4)
            if drum == 1:
                rra = False
                b = 1
                self.tick(7 + 7 + 4)
            else:
                rra = True
                b = drum - 1
                self.tick(12)
            self.tick(13 + 4 + 13 + 7 + 10 + 7)
            d = 0
            for i in range(20):
                a = self.noise[i]
                self.tick(7 + 7)
                for _ in range(b):
                    if rra:
                        a >>= 1
                    self.tick(4 + 4 + 13)
                self.tick(-5)
                a = (a + 1) & 0xff
                self.tick(4 + 4)
                self.tick(13 * (a - 1) + 8 if a else 13 * 255 + 8)
                d ^= 0x10
                self.tick(4 + 7 + 4 + 11)
                self.out_level(1 if d else 0, 'drum')
                self.tick(4 + 6 + 12)
            self.tick(-5)
        else:
            self.tick(12)
        self.b_alt = 5
        self.tick(7 + 10 + 4)
        self.count_duration()

    def count_duration(self):
        self.a_alt = (self.a_alt - 1) & 0xff
        self.tick(4 + 7)
        if self.a_alt != 0:
            self.tick(4 + 4 + 10)
            self.iff = True
            return
        # note over
        self.tick(5 + 7 + 4 + 7)
        if self.echo_rem != 0:
            self.a_alt = self.echo_rem
            self.echo_rem = 0
            self.jr2 = 0x20
            self.tick(11 + 7 + 13 + 4 + 13 + 10 + 12 + 4 + 4 + 10)
            self.iff = True
            return
        self.tick(5 + 10)
        raise NoteOver()


def main():
    code = open(sys.argv[1], 'rb').read()
    rom = open(sys.argv[2], 'rb').read()
    accept = None
    if len(sys.argv) > 4:
        accept = [int(l.split()[1]) for l in open(sys.argv[4]) if l.startswith('F')]
    e = Engine(code, rom, accept_times=accept)
    e.run()
    with open(sys.argv[3], 'w') as f:
        f.write(f'# frames {e.frames}\n')
        for t, lv, tag in e.out:
            f.write(f'{t} {lv} {tag}\n')
    print(f'frames={e.frames} toggles={len(e.out)} end={e.t}')


if __name__ == '__main__':
    main()
