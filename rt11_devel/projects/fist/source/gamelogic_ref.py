"""Reference reproductions of WotEF game-logic routines (path A), validated
against the Z80 simulator by before/after memory comparison.

Game-logic routines read/write the fighter/game state (mostly $9C2x, $AA..,
$B0..), so the validation harness captures memory at a routine's entry, runs
the Python reproduction on a copy, and checks the watched state cells match
the sim's memory after the real routine returns - the same method that proved
the sprite decoder, generalized to whole subroutines.

First port: the round timer $9C6F (+ Time_Tick $9CA0).  Every frame it ticks
a 13-frame divider ($9CA6); each expiry decrements the round time ($9CA5) and,
on zero, raises the timeout flag ($9C2B).  ($9C93 Print_Time draws the digits;
the drawing is not part of the state logic checked here.)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from trace_sprites import build_sim, PC                      # noqa: E402

SP = 12


# ── routine reproductions ─────────────────────────────────────────────────────

def _u16(m, a):
    return m[a] | (m[(a + 1) & 0xFFFF] << 8)


def apply_hit(m, A):
    """$9E7F / $A01C: apply a connected hit - set the opponent's reaction
    action A['react'] (stagger/knockdown) and the hit value $B150."""
    d = m[A['act']]
    react = A['react']
    m[0xAA3F] = d
    m[0xB150] = m[(0xA073 + d) & 0xFFFF]
    type_nz = m[(0xB47E + d) & 0xFFFF] != 0
    if m[A['aface']] == m[A['tface']]:            # same facing
        m[react] = 0x16 if type_nz else 0x1A
    elif type_nz:                                 # facing differ, type != 0
        m[react] = 0x1A
    elif d in (0x18, 0x07, 0x0C):                 # heavy actions
        m[react] = 0x1B
        m[0xB150] = 0x04
    else:
        m[react] = 0x16


# Address sets for the two symmetric hit-detection routines: $9D29 (player 1
# attacks, sets $AA08, applies via $9E7F) and $9ED2 (player 2, $AA48, $A01C).
HIT_P1 = dict(act=0xAA04, g1=0xAA13, g2=0xAA16, g3=0xAA09, fg=0xAA12,
              aface=0xAA17, tface=0xAA57, ridx=0xAA52, result=0xAA08,
              setpos=(0xAA19, 0xAA59), react=0xAA43)
HIT_P2 = dict(act=0xAA44, g1=0xAA53, g2=0xAA56, g3=0xAA49, fg=0xAA52,
              aface=0xAA57, tface=0xAA17, ridx=0xAA12, result=0xAA48,
              setpos=None, react=0xAA03)


def hit_detect(m, A):
    """$9D29 / $9ED2: does this fighter's attack reach the opponent this frame?
    Sets A['result'] (2/1) on a hit and returns True (the real routine then
    tail-calls the apply); returns False on a miss (the routine RETs)."""
    if A['setpos']:                              # $9D29 latches both x-positions
        m[0xA071], m[0xA072] = m[A['setpos'][0]], m[A['setpos'][1]]
    d = m[A['act']]
    if m[(0xA90D + d) & 0xFFFF] == 0:
        return False
    if m[A['g1']] == 0 or m[A['g2']] != 0 or m[A['g3']] != 0:
        return False
    if m[A['fg']] != m[(0xA971 + d) & 0xFFFF]:
        return False
    tbl = 0xA9BC if m[A['aface']] == m[A['tface']] else 0xA98A
    paddr = (tbl + ((d * 2) & 0xFF)) & 0xFFFF
    m[0xA06F], m[0xA070] = m[paddr], m[(paddr + 1) & 0xFFFF]
    reach = m[(_u16(m, 0xA06F) + m[A['ridx']]) & 0xFFFF]
    if reach == 0x80:
        return False
    e = (reach + 0x80) & 0xFF
    if m[A['aface']] != 0:
        dist = (m[0xA071] - m[0xA072]) & 0xFF
    else:
        dist = (m[0xA072] - m[0xA071]) & 0xFF
    c = (dist + 0x80) & 0xFF
    a93f = m[(0xA93F + d) & 0xFFFF]
    a958 = m[(0xA958 + d) & 0xFFFF]
    res = A['result']
    if m[(0xB47E + d) & 0xFFFF] != 0:
        if c == e:
            m[res] = 2; return True
        elif c < e:
            return False
        elif ((c - a93f) & 0xFF) < e:
            m[res] = 2; return True
        elif ((c - a958) & 0xFF) < e:
            m[res] = 1; return True
    else:
        if c == e:
            m[res] = 2; return True
        elif c < e:
            if ((a93f + c) & 0xFF) >= e:
                m[res] = 2; return True
            elif ((a958 + c) & 0xFF) >= e:
                m[res] = 1; return True
    return False


def _f(m, base, off):
    return m[(base + off) & 0xFFFF]


def range_9ba7(m, C, Q):
    """$9BA7: is the opponent within striking distance / valid facing for the
    pending move?  Writes the signed gap to $9C2D and returns 0 (no) / 1 (yes).
    D/E here are local (reloaded from $AA19); the chain PUSHes DE around it."""
    d = _f(m, 0xAA19, C)
    e = _f(m, 0xAA19, Q)
    same_face = m[0xAA57] == m[0xAA17]
    v = (d - e) & 0xFF if _f(m, 0xAA17, C) != 0 else (e - d) & 0xFF
    m[0x9C2D] = v
    t = _f(m, 0xB47E, _f(m, 0xAA04, Q))
    if same_face:                                # $9BF5
        if t == 0:
            return 0
        return 1 if 0x03 <= v < 0x10 else 0
    # facing differ ($9BC0)
    if t != 0:
        return 0
    return 1 if (v >= 0xEF or v < 0x16) else 0


def anim_9920(m, C, Q, D, E):
    """$9920: launch / recover a move once its wind-up frame ($AA0x==3) clears
    and the opponent is in range (via $9BA7); else hold."""
    if E == 0x03:
        if D in (0x13, 0x14):                    # $992E: E := D, hold
            return D, D
        if _f(m, 0xAA16, Q) != 0:
            return D, E
        if _f(m, 0xAA09, Q) != 0:
            return D, E
        a = _f(m, 0xAA04, Q)
        if a == 0x10 or a == 0x0A:
            return D, E
        if _f(m, 0xA90D, a) == 0:
            return D, E
        if range_9ba7(m, C, Q) == 0:             # PUSH/POP preserves D,E
            return D, E
        E = _f(m, 0xA926, _f(m, 0xAA04, Q))      # $9964: new sub-state
        m[(0xAA09 + C) & 0xFFFF] = 0
        m[(0xAA16 + Q) & 0xFFFF] = 0
        return D, E
    # E != 3 ($9986)
    if E != 0x07:
        return D, E
    if D in (0x04, 0x07):
        return D, E
    return D, 0x18


def anim_9994(m, C, Q, D, E):
    """$9994: drive the action D from the current sub-state E - knock-downs,
    transitions, and the once-per-fall hit-credit tick ($9CA7/$B150)."""
    if E in (0x1A, 0x1B, 0x16):                  # $99A1: knock-down landed
        D = E
        m[(0xAA18 + C) & 0xFFFF] = 0x7A
        m[(0xAA16 + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        if _f(m, 0xAA13, C) == 0:
            return D, E
        a = _f(m, 0xAA12, C)
        if a != 0x2C and a != 0x28:
            return D, E
        if m[0x9CA7] != 0:
            return D, E
        m[0x9CA7] = 0x01
        m[0xB150] = 0x05
        return D, E
    # $99DD
    if D == E:
        return D, E
    if D == 0x01:                                # $99E4
        if E == 0x11:
            return 0x12, E
        if E in (0x07, 0x10, 0x0A):
            return 0x04, E
        return E, E
    # $99FD (D != 1)
    if E in (0x07, 0x10, 0x0A):                  # $9A0A
        if D == 0x04:
            if _f(m, 0xAA09, C) != 0x01:
                return D, E
            return E, E
        if D == 0x12:
            m[(0xAA16 + C) & 0xFFFF] = 0x01
            return D, E
        return D, E
    # $9A27
    if D == 0x12 and E == 0x11:
        if _f(m, 0xAA09, C) != 0x01:
            return D, E
        return E, E
    if D == 0x11 and _f(m, 0xAA09, C) == 0x01:   # $9A4D
        m[(0xAA16 + C) & 0xFFFF] = 0x01
        m[(0xAA07 + C) & 0xFFFF] = 0x00
        m[(0xAA0B + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        return 0x15, E
    t = _f(m, 0xB462, D)                          # $9A70
    if t == 0x80:
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        if E == 0x11:
            return 0x12, E
        return E, E
    if t != 0:                                   # $9A8D
        m[(0xAA09 + C) & 0xFFFF] = 0x00
        return D, E
    m[(0xAA16 + C) & 0xFFFF] = 0x01
    m[(0xAA09 + C) & 0xFFFF] = 0x00
    return D, E


def anim_9aa1(m, C, Q, D, E):
    """$9AA1: commit the chosen action D into the animation slot ($AA0B/$AA0C),
    resetting the frame counter when the action changes."""
    a = _f(m, 0xAA0B, C)
    if a != D:
        m[(0xAA0C + C) & 0xFFFF] = D
        m[(0xAA0B + C) & 0xFFFF] = 0x00
        m[(0xAA09 + C) & 0xFFFF] = 0x00
    elif _f(m, 0xAA09, C) == 0x01:
        m[(0xAA0C + C) & 0xFFFF] = 0x00
    else:
        m[(0xAA0C + C) & 0xFFFF] = _f(m, 0xAA0B, C)
    return D, E


def update_fighter(m, C):
    """$97BB: per-fighter animation update.  Loads (D,E)=($AA04+C,$AA05+C),
    threads it through the three state machines, stores it back ($9B9D)."""
    Q = m[0x9C29]
    D = _f(m, 0xAA04, C)
    E = _f(m, 0xAA05, C)
    D, E = anim_9920(m, C, Q, D, E)
    D, E = anim_9994(m, C, Q, D, E)
    D, E = anim_9aa1(m, C, Q, D, E)
    m[(0xAA04 + C) & 0xFFFF] = D & 0xFF
    m[(0xAA05 + C) & 0xFFFF] = E & 0xFF


def recover_9ad7(m, C):
    """$9AD7: second per-fighter pass - get-up / recovery state reset, applied
    when the move-pending flag $AA0D+C is set.  Loads (D,E) ($9B93), and the
    orchestrator stores the result back ($9B9D)."""
    D = _f(m, 0xAA04, C)
    E = _f(m, 0xAA05, C)
    # D in (2,3) does a discarded compare ($AA36+C-$AA19+C); no state change.
    if _f(m, 0xAA0D, C) != 0:
        m[(0xAA0D + C) & 0xFFFF] = 0
        a03 = _f(m, 0xAA03, C)
        if a03 != 0:                             # $9B09: queued reaction
            E = a03
            m[0x9C28] = a03
        elif _f(m, 0xAA16, C) != 0:              # $9B0E
            if D == 0x11:
                m[(0xAA17 + C) & 0xFFFF] ^= 0x01
            m[(0xAA07 + C) & 0xFFFF] = 0
            m[(0xAA09 + C) & 0xFFFF] = 0
            m[(0xAA0B + C) & 0xFFFF] = 0
            m[(0xAA0C + C) & 0xFFFF] = 0x01
            m[(0xAA16 + C) & 0xFFFF] = 0
            D = 0x01
        elif _f(m, 0xB462, D) != 0:              # $9B67
            m[(0xAA00 + C) & 0xFFFF] = 0x01
            m[(0xAA07 + C) & 0xFFFF] = 0
            m[(0xAA0B + C) & 0xFFFF] = 0
            D = 0x01
        else:                                    # $9B8A
            m[(0xAA09 + C) & 0xFFFF] = 0x01
    m[(0xAA04 + C) & 0xFFFF] = D & 0xFF           # $9B9D store (orchestrator)
    m[(0xAA05 + C) & 0xFFFF] = E & 0xFF


def score_calc(m, de, b):
    """$AFC2: add point value `b` (BCD) into the 3-byte little-endian BCD score
    display buffer at `de`.  10 ($0A) is remapped to BCD 16 ($10).  Reproduced
    as decimal arithmetic - equivalent to the Z80 ADD/DAA chain for valid BCD."""
    if b == 0x0A:
        b = 0x10
    val = 0
    for i in (2, 1, 0):
        byte = m[(de + i) & 0xFFFF]
        val = val * 100 + (byte >> 4) * 10 + (byte & 0x0F)
    val += (b >> 4) * 10 + (b & 0x0F)
    for i in range(3):
        d = val % 100
        m[(de + i) & 0xFFFF] = ((d // 10) << 4) | (d % 10)
        val //= 100


def award_points(m):
    """$AF36: award a yin-yang point.  The score flag $AA08 (P1) / $AA48 (P2)
    is 1 = half point, 2 = full; the value comes from $B00B[attack $AA3F],
    halved for a half point, accumulated into $AA02 / $AA42 (and the BCD
    display buffer).  Only one fighter is credited per call.  Display draws
    (gated by $9C2C) are a separate layer, not reproduced here."""
    if m[0xAA08] != 0:                            # P1 scored
        b = m[(0xB00B + m[0xAA3F]) & 0xFFFF]
        if m[0xAA08] == 0x01:
            b >>= 1
        m[0xAA02] = (m[0xAA02] + b) & 0xFF
        score_calc(m, 0xB02D, b)
        m[0xAA08] = 0
        return
    if m[0xAA48] != 0:                            # P2 scored ($AF7D)
        b = m[(0xB00B + m[0xAA3F]) & 0xFFFF]
        if m[0xAA48] == 0x01:
            b >>= 1
        m[0xAA42] = (m[0xAA42] + b) & 0xFF
        score_calc(m, 0xB030, b)
        m[0xAA48] = 0


class _Rnd:
    """Replay a recorded sequence of $A3FF (refresh-register RNG) return values.
    Lets the AI decision LOGIC be validated bit-exactly while abstracting the
    RNG SOURCE (the MS-0515 port has no Z80 R register and will use its own)."""
    __slots__ = ('v', 'i')

    def __init__(self, v):
        self.v = v
        self.i = 0

    def __call__(self):
        x = self.v[self.i]
        self.i += 1
        return x


def ai_a47c(m, a):
    """$A47C: if the opponent ($A62F) is mid-kick, remap the chosen action
    through the $B449 counter-table; otherwise keep it."""
    if m[0xA62F] in (0x0A, 0x10, 0x04, 0x07):
        return m[(0xB449 + a) & 0xFFFF]
    return a


def ai_a583(m):
    """$A583: AI range/facing check (mirror of $9BA7 on the AI scratch state).
    Returns 1 if the opponent is in striking distance, else 0."""
    d2 = (m[0xA60A] << 1) & 0xFF
    e2 = (m[0xA644] << 1) & 0xFF
    same = m[0xA642] == m[0xA608]
    if m[0xA608] != 0:
        a = (d2 - e2) & 0xFF
    else:
        a = (e2 - d2) & 0xFF
    m[0xA5EE] = a
    t = m[(0xB47E + m[0xA62F]) & 0xFFFF]
    if same:
        if t == 0:
            return 0
        return 1 if 0x03 <= a < 0x10 else 0
    if t != 0:
        return 0
    return 1 if (a >= 0xEF or a < 0x16) else 0


def ai_decide(m, randoms):
    """$A090: AI opponent per-frame move-selection.  Picks the move intent
    ($A5F6, and often $A5F1) from the fighter scratch state + opponent action,
    using weighted random choices (replayed $A3FF stream).  Returns True if it
    reached the $A553 special-state dispatcher (deferred: computed jump).
    `randoms` is a list (own stream) or a callable (a shared stream, so the two
    per-frame AI calls draw the recorded $A3FF sequence continuously)."""
    rnd = randoms if callable(randoms) else _Rnd(randoms)
    reg = {'a': 0, 'b': 0}
    label = 'A090'
    # $A553 special-state dispatcher: computed jump through $B3A9[$A618].
    A553_TARGETS = {1: 'A49A', 2: 'A53E', 3: 'A4C8', 4: 'A4D5', 5: 'A4E2',
                    6: 'A50C', 7: 'A524', 8: 'A4FE', 9: 'A560'}
    while label is not None:
        if label == 'A553':
            label = A553_TARGETS[m[0xA618]]
        elif label == 'A49A':
            if m[0xA614] < 7:
                label = 'A4BE'
            elif m[0xA63D] == 0x19:
                label = 'A4B1'
            else:
                label = 'A4A8'
        elif label == 'A4A8':
            m[0xA5F6] = 0x04
            m[0xA5F1] = 0x04
            return False
        elif label == 'A4B1':
            m[0xA5F6] = 0x0A
            m[0xA5F1] = 0x0A
            m[0xA618] = 0
            return False
        elif label == 'A4BE':
            label = 'A4A8' if m[0xA62F] != 1 else 'A4B1'
        elif label == 'A4C8':
            m[0xA5F6] = 0x0E
            m[0xA5F1] = 0x0E
            m[0xA618] = 0
            return False
        elif label == 'A4D5':
            m[0xA5F1] = 0x09
            m[0xA5F6] = 0x09
            m[0xA618] = 0
            return False
        elif label == 'A4E2':
            if m[0xA5FA] != 0:
                m[0xA5F1] = 0x07
                m[0xA5F6] = 0x07
                m[0xA618] = 0
            else:
                m[0xA5F6] = 0x04
                m[0xA5F1] = 0x04
            return False
        elif label == 'A4FE':
            rv = rnd()
            if rv < 0x80:
                label = 'A50C'
            else:
                m[0xA618] = 0x05
                label = 'A4E2'
        elif label == 'A50C':
            rnd()                        # consumed; the JP $A517 is unconditional
            m[0xA5F6] = 0x18
            m[0xA5F1] = 0x18
            m[0xA618] = 0
            return False
        elif label == 'A524':
            a = 0x0A if m[0xA608] == m[0xA642] else 0x10
            m[0xA5F1] = a
            m[0xA5F6] = a
            m[0xA618] = 0
            return False
        elif label == 'A53E':
            if m[0xA5F5] == 0x0A:
                m[0xA618] = 0
            else:
                m[0xA5F6] = 0x0A
                m[0xA5F1] = 0x0A
            return False
        elif label == 'A560':
            if m[0xA614] < 7:
                label = 'A57B'
            elif rnd() & 0x80:
                label = 'A57B'
            else:
                m[0xA618] = 0
                m[0xA5F1] = 0x0B
                m[0xA5F6] = 0x0B
                return False
        elif label == 'A57B':
            m[0xA618] = 0x01
            label = 'A49A'
        elif label == 'A090':
            a = m[0xA5F4]
            if a != 0:
                m[0xA5F6] = a
                return False
            m[0xA5EC] = m[0xA60A]
            m[0xA5ED] = m[0xA644]
            if m[0xA62E] != 0:
                if m[0xA5F5] != 0x0E:
                    m[0xA616] = (m[0xA616] - 1) & 0xFF
                    if m[0xA616] != 0:
                        return False
                m[0xA5F6] = 0x01
                return False
            if m[0xA618] != 0:
                label = 'A553'
            elif m[0xA641] != 0:
                label = 'A1B5'
            elif m[(0xA90D + m[0xA62F]) & 0xFFFF] == 0:
                label = 'A1B5'
            elif m[0xA5F5] in (0x13, 0x14):
                label = 'A0E4'
            else:
                label = 'A0FC'
        elif label == 'A0E4':
            if m[(0xA926 + m[0xA62F]) & 0xFFFF] == m[0xA5F5]:
                label = 'A13E'
            else:
                m[0xA5F6] = 0x01
                m[0xA5F1] = 0x01
                return False
        elif label == 'A0FC':
            a = m[0xA5F5]
            if a == 0x12:
                label = 'A1B5'
            elif a in (0x01, 0x03, 0x02):
                label = 'A145'
            elif m[0xA5FA] != 0 or m[0xA607] != 0:
                label = 'A0F3'
            elif m[0xA62F] in (0x0A, 0x10):
                label = 'A127'
            else:
                label = 'A13E'
        elif label == 'A0F3':
            m[0xA5F6] = 0x01
            m[0xA5F1] = 0x01
            return False
        elif label == 'A127':
            if m[0xA5F5] in (0x0A, 0x10, 0x04, 0x07, 0x0B):
                label = 'A13E'
            else:
                label = 'A0F3'
        elif label == 'A13E':
            m[0xA5F6] = m[0xA5F1]
            return False
        elif label == 'A145':
            if ai_a583(m) == 0:
                label = 'A1B5'
            else:
                a61b = m[0xA61B]
                if a61b == 0:
                    label = 'A16D'
                elif (a61b & 0x80) == 0:
                    label = 'A161'
                else:
                    rv = rnd() & m[0xA60B]
                    if rv == 0:
                        label = 'A16D'
                    else:
                        m[0xA61B] = rv
                        label = 'A161'
        elif label == 'A161':
            m[0xA61B] = (m[0xA61B] - 1) & 0xFF
            if m[0xA61B] != 0:
                label = 'A1B5'
            else:
                m[0xA61B] = 0x80
                label = 'A16D'
        elif label == 'A16D':
            rv = rnd() & m[0xA646]
            if rv & 0x80:
                label = 'A2E7'
            elif (rv & 0x70) != 0:
                label = 'A1A5'
            elif (rv & 0x0F) != 0:
                label = 'A195'
            else:
                rv2 = rnd()
                a = 0x09 if ((rv2 & 0x80) or rv2 == 0) else 0x0B
                m[0xA5F1] = a
                m[0xA5F6] = a
                return False
        elif label == 'A195':
            a = m[(0xB3BD + m[0xA62F]) & 0xFFFF]
            if a == 0:
                label = 'A1A5'
            else:
                m[0xA618] = a
                return False
        elif label == 'A1A5':
            a = m[(0xA926 + m[0xA62F]) & 0xFFFF]
            m[0xA5F6] = a
            m[0xA5F1] = a
            return False
        elif label == 'A1B5':
            if m[0xA607] != 0:
                label = 'A22A'
            elif m[0xA5FA] == 0:
                label = 'A21B'
            elif m[0xA5F5] == 0x04:
                label = 'A22A'
            elif m[0xA5F5] != 0x12:
                label = 'A1F4'
            elif m[0xA60E] == 0:
                label = 'A22A'
            else:
                m[0xA5F1] = 0x01
                m[0xA5F6] = 0x01
                m[0xA5F5] = 0x01
                m[0xA5FD] = 0x01
                m[0xA608] ^= 0x01
                m[0xA5FA] = 0
                m[0xA60E] = 0
                m[0xA5FC] = 0
                return False
        elif label == 'A1F4':
            if (m[0xA61C] & 0x80) == 0:
                label = 'A207'
            else:
                m[0xA61C] = rnd() & m[0xA60F]
                label = 'A22A'
        elif label == 'A207':
            m[0xA61C] = (m[0xA61C] - 1) & 0xFF
            if m[0xA61C] != 0:
                label = 'A22A'
            else:
                m[0xA5F1] = 0x01
                m[0xA5F6] = 0x01
                m[0xA61C] = 0x80
                return False
        elif label == 'A21B':
            if m[0xA5F5] in (0x01, 0x03, 0x02):
                label = 'A231'
            else:
                label = 'A22A'
        elif label == 'A22A':
            m[0xA5F6] = m[0xA5F1]
            return False
        elif label == 'A231':
            a608 = m[0xA608]
            if a608 == m[0xA642]:
                label = 'A2A0'
            else:
                if a608 != 0:
                    a = (m[0xA5EC] - m[0xA5ED]) & 0xFF
                else:
                    a = (m[0xA5ED] - m[0xA5EC]) & 0xFF
                m[0xA5EE] = a
                if a >= 0xD5 or a < 0x15:
                    label = 'A2C7'
                elif a >= 0x80:
                    label = 'A292'
                else:
                    label = 'A25D'
        elif label == 'A25D':
            m[0xA610] = (m[0xA610] - 1) & 0xFF
            if m[0xA610] != 0:
                label = 'A22A'
            elif m[0xA5F5] != 0x02:
                label = 'A27F'
            else:
                rv = rnd() & m[0xA61A]
                if rv == 0:
                    label = 'A27F'
                else:
                    m[0xA610] = rv
                    m[0xA5F6] = 0x01
                    m[0xA5F1] = 0x01
                    return False
        elif label == 'A27F':
            m[0xA610] = rnd() & m[0xA619]
            m[0xA5F6] = 0x02
            m[0xA5F1] = 0x02
            return False
        elif label == 'A292':
            m[0xA5F1] = 0x12
            m[0xA5F6] = 0x12
            m[0xA60E] = 0x01
            return False
        elif label == 'A2A0':
            if m[0xA608] != 0:
                a = (m[0xA5ED] - m[0xA5EC]) & 0xFF
            else:
                a = (m[0xA5EC] - m[0xA5ED]) & 0xFF
            m[0xA5EE] = a
            if a >= 0xDF or a < 0x1F:
                label = 'A2C7'
            elif a >= 0x80:
                label = 'A25D'
            else:
                label = 'A292'
        elif label == 'A2C7':
            if (m[0xA611] & 0x80) == 0:
                label = 'A2DB'
            else:
                m[0xA611] = rnd() & m[0xA617]
                label = 'A35F'
        elif label == 'A2DB':
            m[0xA611] = (m[0xA611] - 1) & 0xFF
            if m[0xA611] != 0:
                label = 'A35F'
            else:
                m[0xA611] = 0x80
                label = 'A2E7'
        elif label == 'A2E7':
            if m[0xA608] == m[0xA642]:
                label = 'A3AF'
            elif m[0xA62F] == 0x13:
                label = 'A313'
            elif m[0xA62F] != 0x14:
                label = 'A325'
            else:
                a = m[(0xA904 + (rnd() & 0x03)) & 0xFFFF]
                if a == 0x07:
                    label = 'A3A1'
                else:
                    m[0xA5F1] = a
                    m[0xA5F6] = a
                    return False
        elif label == 'A313':
            a = m[(0xA900 + (rnd() & 0x03)) & 0xFFFF]
            m[0xA5F1] = a
            m[0xA5F6] = a
            return False
        elif label == 'A325':
            m[0xA613] = (m[0xA5EE] + 0x33) & 0xFF
            a = (rnd() & m[0xA612])
            a = (a + m[0xA613]) & 0xFF
            a = m[(0xB300 + a) & 0xFFFF]
            if a == 0x0E:
                reg['a'] = a
                label = 'A383'
            else:
                a = ai_a47c(m, a)
                if a == 0x0A:
                    label = 'A390'
                elif a == 0x07:
                    label = 'A3A1'
                elif a in (0x0F, 0x10):
                    reg['a'] = a
                    label = 'A3DA'
                else:
                    m[0xA5F1] = a
                    m[0xA5F6] = a
                    return False
        elif label == 'A35F':
            if m[0xA641] != 0 or m[0xA634] != 0:
                label = 'A22A'
            elif m[(0xA90D + m[0xA62F]) & 0xFFFF] == 0:
                label = 'A22A'
            else:
                m[0xA5F6] = 0x01
                m[0xA5F1] = 0x01
                return False
        elif label == 'A383':
            reg['b'] = reg['a']
            a614 = m[0xA614]
            reg['a'] = reg['b']
            if a614 < 7:
                label = 'A358'
            else:
                reg['a'] = 0x02
                if a614 != 7:
                    label = 'A358'
                else:
                    label = 'A390'
        elif label == 'A358':
            m[0xA5F1] = reg['a']
            m[0xA5F6] = reg['a']
            return False
        elif label == 'A390':
            if m[0xA614] >= 2:
                m[0xA5F1] = 0x0A
                m[0xA5F6] = 0x0A
            return False
        elif label == 'A3A1':
            m[0xA618] = 0x05
            m[0xA5F6] = 0x04
            m[0xA5F1] = 0x04
            return False
        elif label == 'A3AF':
            m[0xA613] = (m[0xA5EE] + 0x29) & 0xFF
            a = (rnd() & m[0xA612])
            a = (a + m[0xA613]) & 0xFF
            a = m[(0xB352 + a) & 0xFFFF]
            a = ai_a47c(m, a)
            if a in (0x0F, 0x10):
                reg['a'] = a
                label = 'A3DA'
            else:
                m[0xA5F1] = a
                m[0xA5F6] = a
                return False
        elif label == 'A3DA':
            reg['b'] = reg['a']
            rv = rnd() & m[0xA615]
            if rv & 0x80:
                label = 'A292'
            elif rv >= 0x40:
                label = 'A3F7'
            else:
                if rv >= 0x20:
                    reg['b'] = 0x08
                m[0xA5F6] = reg['b']
                m[0xA5F1] = reg['b']
                return False
        elif label == 'A3F7':
            m[0xA5F6] = 0x03
            m[0xA5F1] = 0x03
            return False
        else:
            raise RuntimeError(f"unhandled AI label {label}")
    return False


def pos_update(m, hl):
    """$9698 (the position-update tail of the animation advance $95E1): add the
    per-frame velocity (from the record at m[hl+4..5]+3, sign by m[hl+6]^m[hl+7])
    to the position m[hl+9], clamp the BYTE result to the screen: >= $C8 wraps
    to 0, $5F..$C7 pins to the $5F edge, < $5F is kept.  (The ADD/SUB carry is
    discarded by the AND A at $96BB.)  hl is the animation-block pointer at
    entry (register-threaded)."""
    ptr = m[(hl + 4) & 0xFFFF] | (m[(hl + 5) & 0xFFFF] << 8)
    flag = m[(hl + 6) & 0xFFFF] ^ m[(hl + 7) & 0xFFFF]
    m[(hl + 8) & 0xFFFF] = (m[(ptr + 2) & 0xFFFF] + m[(hl + 8) & 0xFFFF]) & 0xFF
    b = m[(ptr + 3) & 0xFFFF]
    pos = m[(hl + 9) & 0xFFFF]
    r = ((pos + b) if flag == 0 else (pos - b)) & 0xFF
    if r >= 0xC8:
        new = 0
    elif r < 0x5F:
        new = r
    else:
        new = 0x5F
    m[(hl + 9) & 0xFFFF] = new


def _sort2(a, b):
    """$C1F6: return (min, max) of the two bytes."""
    return (a, b) if a <= b else (b, a)


def bf13(m):
    """$BF13 (the logic->graphics bridge): copy each fighter's logic state
    ($AA..) into the render area ($C41B-$C420), look up the pose record pointers
    ($C428/$C42A via the $C440 table + $44CC base), and build the screen
    bounding box ($C434-$C43B) spanning this frame's and last frame's positions
    (for erase+redraw); then save this frame's positions to $C42C-$C433."""
    m[0xC425] = m[0xAA52]; m[0xC426] = 0
    m[0xC41D] = m[0xAA59]
    m[0xC41E] = m[0xAA58]
    m[0xC420] = m[0xAA57]
    m[0xC423] = m[0xAA12]; m[0xC424] = 0
    m[0xC41B] = m[0xAA19]
    m[0xC41C] = m[0xAA18]
    m[0xC41F] = m[0xAA17]
    p1 = (0x44CC + _u16(m, (0xC440 + (_u16(m, 0xC423) << 1)) & 0xFFFF)) & 0xFFFF
    m[0xC428] = p1 & 0xFF; m[0xC429] = (p1 >> 8) & 0xFF
    p2 = (0x44CC + _u16(m, (0xC440 + (_u16(m, 0xC425) << 1)) & 0xFFFF)) & 0xFFFF
    m[0xC42A] = p2 & 0xFF; m[0xC42B] = (p2 >> 8) & 0xFF
    ix, iy = _u16(m, 0xC428), _u16(m, 0xC42A)
    ix1, ix2 = m[(ix + 1) & 0xFFFF], m[(ix + 2) & 0xFFFF]
    iy1, iy2 = m[(iy + 1) & 0xFFFF], m[(iy + 2) & 0xFFFF]
    m[0xC434] = _sort2(m[0xC41B], m[0xC42C])[0]
    m[0xC435] = _sort2((m[0xC41B] + ix1) & 0xFF, m[0xC42D])[1]
    m[0xC436] = _sort2(m[0xC41C], m[0xC42E])[0]
    m[0xC437] = 0xBE
    m[0xC438] = _sort2(m[0xC41D], m[0xC430])[0]
    m[0xC439] = _sort2((m[0xC41D] + iy1) & 0xFF, m[0xC431])[1]
    m[0xC43A] = _sort2(m[0xC41E], m[0xC432])[0]
    m[0xC43B] = 0xBE
    m[0xC42C] = m[0xC41B]
    m[0xC42D] = (m[0xC41B] + ix1) & 0xFF
    m[0xC42E] = m[0xC41C]
    m[0xC42F] = (m[0xC41C] + ix2) & 0xFF
    m[0xC430] = m[0xC41D]
    m[0xC431] = (m[0xC41D] + iy1) & 0xFF
    m[0xC432] = m[0xC41E]
    m[0xC433] = (m[0xC41E] + iy2) & 0xFF


def _mcpy(m, src, dst, n):
    for i in range(n):
        m[(dst + i) & 0xFFFF] = m[(src + i) & 0xFFFF]


def move_select(m, randoms):
    """$983D (the per-fighter move-selection part of $97CB): for each fighter a
    queued reaction ($AA03/$AA43) forces the move; else if AI-controlled
    ($AA06/$AA46) the AI ($A090) decides on a scratch copy (the save/restore
    memcpy wrappers $AADC/$AB0A/$AAF3/$AB16 etc.), whose chosen $A5F6 lands in
    $AA05/$AA45 via the restore; else keyboard (not modelled - AI demo).  The
    two AI calls share one random stream."""
    rnd = randoms if callable(randoms) else _Rnd(randoms)
    if m[0xAA03] != 0:
        m[0xAA05] = m[0xAA03]
    elif m[0xAA06] != 0:
        _mcpy(m, 0xAA00, 0xA5F1, 0x1A)           # $AADC
        _mcpy(m, 0xAA8B, 0xA60B, 0x12)           # $AB0A
        ai_decide(m, rnd)                        # $A090
        _mcpy(m, 0xA5F1, 0xAA00, 0x1A)           # $AAF3
        _mcpy(m, 0xA60B, 0xAA8B, 0x12)           # $AB16
    if m[0xAA43] != 0:
        m[0xAA45] = m[0xAA43]
    elif m[0xAA46] != 0:
        _mcpy(m, 0xAA40, 0xA5F1, 0x1A)           # $AB22
        _mcpy(m, 0xAA77, 0xA60B, 0x12)           # $AB50
        ai_decide(m, rnd)                        # $A090
        _mcpy(m, 0xA5F1, 0xAA40, 0x1A)           # $AB39
        _mcpy(m, 0xA60B, 0xAA77, 0x12)           # $AB5C


def ai_load_params(m):
    """$A402: load the current AI opponent's 'personality' parameters into the
    working state ($A60E-$A61C) from eight tables indexed by the AI id $A614.
    Pure table lookup + constants; the decision logic runs on these later."""
    b = m[0xA614]
    m[0xA60F] = m[(0xB3D6 + b) & 0xFFFF]
    m[0xA616] = 0x02
    m[0xA612] = m[(0xB3EC + b) & 0xFFFF]
    m[0xA615] = m[(0xB426 + b) & 0xFFFF]
    m[0xA61C] = 0x80
    m[0xA5F1] = 0x01
    m[0xA617] = m[(0xB401 + b) & 0xFFFF]
    m[0xA611] = 0x80
    m[0xA60E] = 0x00
    m[0xA618] = 0x00
    m[0xA646] = m[(0xB432 + b) & 0xFFFF]
    m[0xA619] = m[(0xB40D + b) & 0xFFFF]
    m[0xA61A] = m[(0xB41A + b) & 0xFFFF]
    m[0xA610] = 0x01
    m[0xA61B] = 0x80
    m[0xA60B] = m[(0xB43E + b) & 0xFFFF]


def reset_frame_9ca8(m):
    """$9CA8 head: reset both fighters to per-frame defaults before the input/AI
    chain runs.  Clears the transient flags, parks both at the idle stance
    (action $17) and default positions/facing.  ($A645 gets the R-register RNG
    seed - excluded from validation.)"""
    for a in (0x9C2B, 0x9CA7, 0xAA4D, 0xAA0D, 0xAA03, 0xAA43, 0xAA16, 0xAA56,
              0xAA17, 0xAA0B, 0xAA4B, 0xAA09, 0xAA49, 0x9C28):
        m[a] = 0
    m[0xAA19] = 0x20
    m[0xAA59] = 0x3C
    m[0xAA18] = m[0xAA58] = 0x7A
    for a in (0xAA0C, 0xAA4C, 0xAA05, 0xAA45, 0xAA04, 0xAA44):
        m[a] = 0x17
    m[0xAA0A] = m[0xAA4A] = 0x01
    m[0xAA57] = 0x01


def reset_acf0(m):
    """$ACF0: reset the two fighters' positions/flags for a new exchange after a
    knockdown - clear the per-fighter recovery/guard flags, park both upright
    ($AA18/$AA58 = $7A).  (The trailing $AD0B redraw-wait loop is excluded.)"""
    for a in (0xAA0D, 0xAA0B, 0xAA4B, 0xAA16, 0xAA56):
        m[a] = 0
    m[0xAA18] = m[0xAA58] = 0x7A


def init_time(m):
    """$AEF8: set the round timer to 30 ($1E) seconds (the $9C93 draw follows)."""
    m[0x9CA5] = 0x1E


def time_tick(m):
    """$9CA0: decrement the round-time counter $9CA5."""
    m[0x9CA5] = (m[0x9CA5] - 1) & 0xFF


def rank_tick(m):
    """$AF27: advance the dan/round counter $AF34 (1..3, wrapping 4 -> 1)."""
    m[0xAF34] = (m[0xAF34] + 1) & 0xFF
    if m[0xAF34] == 0x04:
        m[0xAF34] = 0x01


def contact_flag(m):
    """$AE2E..$AE5C: set the fighter-contact flag $C427 (gates the close-combat
    render path) from the two actions.  On if either fighter is mid-strike
    ($A90D action-flag set) or P1 is in the $11 lunge; off otherwise (P2's $11
    or both idle)."""
    if m[0xAA44] == 0x11:
        m[0xC427] = 0
    elif m[0xAA04] == 0x11:
        m[0xC427] = 1
    elif m[(0xA90D + m[0xAA04]) & 0xFFFF] != 0:
        m[0xC427] = 0
    elif m[(0xA90D + m[0xAA44]) & 0xFFFF] != 0:
        m[0xC427] = 1
    else:
        m[0xC427] = 0


def yinyang_total(m):
    """$900E: yin-yang total controller (state part).  Each score event adds the
    flag $AA08 (P1) / $AA48 (P2) to the running half-point total $AA01 / $AA41
    (0..4); the image draws via $9255 are a separate layer.  Total 2 = one full
    yin-yang, total 4 = two full = a won round.  One fighter per call."""
    if m[0xAA08] != 0:
        m[0xAA01] = (m[0xAA01] + m[0xAA08]) & 0xFF
        return
    if m[0xAA48] != 0:
        m[0xAA41] = (m[0xAA41] + m[0xAA48]) & 0xFF


def new_round(m):
    """$909E (state part): clear both fighters' yin-yang totals and scores."""
    m[0xAA01] = m[0xAA41] = m[0xAA02] = m[0xAA42] = 0


def update_timer(m):
    """$9C6F: round-timer tick (state only; skips the Print_Time draw)."""
    if m[0x9C2B] != 0:
        return
    if (m[0xAA03] | m[0xAA43]) != 0:
        return
    if m[0xAA04] == 0x17:
        return
    m[0x9CA6] = (m[0x9CA6] - 1) & 0xFF
    if m[0x9CA6] != 0:
        return
    m[0x9CA6] = 0x0D
    m[0x9CA5] = (m[0x9CA5] - 1) & 0xFF
    if m[0x9CA5] == 0:
        m[0x9C2B] = 1


# ── validation harness ────────────────────────────────────────────────────────

def validate(addr, pyfunc, watch, want=200, stops=(), budget=4000000, until=()):
    """Run the game; for each call to `addr`, run the real routine until it
    RETs (or reaches a PC in `stops`, e.g. a tail-call), run pyfunc on a copy
    of the entry memory, and compare the `watch` cells.  `until` overrides the
    RET stop with an explicit set of PCs - used to run past a trailing helper
    call (e.g. the orchestrator's $9B9D store after $9AD7)."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    stops = set(stops)
    until = set(until)
    tested = match = 0
    for _ in range(budget):
        if regs[PC] == addr:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            entry = {'A': regs[0], 'B': regs[2], 'C': regs[3], 'D': regs[4],
                     'E': regs[5], 'H': regs[6], 'L': regs[7]}
            before = bytes(memory)
            for _ in range(200000):
                ops[memory[regs[PC]]]()
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if until:
                    if regs[PC] in until:
                        break
                elif (regs[PC] == ret and regs[SP] == s0 + 2) or regs[PC] in stops:
                    break
            mine = bytearray(before)
            pyfunc(mine, entry)
            tested += 1
            if all(mine[a] == memory[a] for a in watch):
                match += 1
            elif tested - match <= 3:
                bad = [(hex(a), memory[a], mine[a]) for a in watch
                       if mine[a] != memory[a]]
                print(f"  MISMATCH: {bad}")
            if tested >= want:
                break
            continue
        ops[memory[regs[PC]]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    return match, tested


FIGHTER_WATCH = list(range(0xAA00, 0xAA80)) + [0x9C2D, 0x9CA7, 0xB150]


def validate_ai(addr, pyfunc, watch, want=200, budget=4000000):
    """Like validate(), but records the $A3FF (R-register RNG) return sequence
    during each call and replays it into pyfunc(mem, randoms) - so the AI
    decision logic is checked bit-exactly while the RNG source is abstracted.
    pyfunc returns True to mark a call 'deferred' (reached the unported $A553
    special-state dispatcher); those are counted separately, not as mismatches."""
    sim, mem = build_sim(watch=(0, 0))
    regs, memory, ops = sim.registers, sim.memory, sim.opcodes
    fd, ia = sim.frame_duration, sim.int_active
    tested = match = deferred = 0
    for _ in range(budget):
        if regs[PC] == addr:
            s0 = regs[SP]
            ret = memory[s0] | (memory[s0 + 1] << 8)
            before = bytes(memory)
            randoms = []
            for _ in range(200000):
                cur = regs[PC]
                ops[memory[cur]]()
                if cur == 0xA3FF:                # just ran LD A,R
                    randoms.append(regs[0])
                if regs[26] and regs[25] % fd < ia:
                    sim.accept_interrupt(regs, memory, regs[PC])
                if regs[PC] == ret and regs[SP] == s0 + 2:
                    break
            mine = bytearray(before)
            if pyfunc(mine, randoms):
                deferred += 1
            else:
                tested += 1
                if all(mine[a] == memory[a] for a in watch):
                    match += 1
                elif tested - match <= 4:
                    bad = [(hex(a), memory[a], mine[a]) for a in watch
                           if mine[a] != memory[a]]
                    print(f"  MISMATCH (rnd={randoms}): {bad}")
            if tested + deferred >= want:
                break
            continue
        cur = regs[PC]
        ops[memory[cur]]()
        if regs[26] and regs[25] % fd < ia:
            sim.accept_interrupt(regs, memory, regs[PC])
    return match, tested, deferred


def main():
    results = []

    def run(addr, label, pyfunc, watch, **kw):
        m, t = validate(addr, pyfunc, watch, **kw)
        print(f"{label:22} {m}/{t} calls match")
        results.append((m, t))

    run(0x9C6F, "$9C6F round timer:", lambda mm, r: update_timer(mm),
        [0x9CA6, 0x9CA5, 0x9C2B])
    run(0x9D29, "$9D29 hit-detect P1:", lambda mm, r: hit_detect(mm, HIT_P1),
        [0xAA08, 0xA06F, 0xA070, 0xA071, 0xA072], stops=[0x9E7F])
    run(0x9ED2, "$9ED2 hit-detect P2:", lambda mm, r: hit_detect(mm, HIT_P2),
        [0xAA48, 0xA06F, 0xA070], stops=[0xA01C])
    run(0x9E7F, "$9E7F apply-hit P1:", lambda mm, r: apply_hit(mm, HIT_P1),
        [0xAA3F, 0xB150, 0xAA43])
    run(0xA01C, "$A01C apply-hit P2:", lambda mm, r: apply_hit(mm, HIT_P2),
        [0xAA3F, 0xB150, 0xAA03])
    run(0x97BB, "$97BB anim update:", lambda mm, r: update_fighter(mm, r['C']),
        FIGHTER_WATCH)
    run(0x9AD7, "$9AD7 recover pass:", lambda mm, r: recover_9ad7(mm, r['C']),
        FIGHTER_WATCH + [0x9C28], until=[0x97A0, 0x97AC])
    run(0xAF36, "$AF36 award points:", lambda mm, r: award_points(mm),
        [0xAA02, 0xAA42, 0xAA08, 0xAA48,
         0xB02D, 0xB02E, 0xB02F, 0xB030, 0xB031, 0xB032])
    run(0x900E, "$900E yin-yang total:", lambda mm, r: yinyang_total(mm),
        [0xAA01, 0xAA41])
    run(0x909E, "$909E new round:", lambda mm, r: new_round(mm),
        [0xAA01, 0xAA41, 0xAA02, 0xAA42])
    run(0x9CA8, "$9CA8 frame reset:", lambda mm, r: reset_frame_9ca8(mm),
        [0x9C2B, 0x9CA7, 0xAA4D, 0xAA0D, 0xAA03, 0xAA43, 0xAA16, 0xAA56,
         0xAA17, 0xAA0B, 0xAA4B, 0xAA09, 0xAA49, 0x9C28, 0xAA19, 0xAA59,
         0xAA18, 0xAA58, 0xAA0C, 0xAA4C, 0xAA05, 0xAA45, 0xAA04, 0xAA44,
         0xAA0A, 0xAA4A, 0xAA57], until=[0x9D0B])
    run(0xA402, "$A402 AI param load:", lambda mm, r: ai_load_params(mm),
        [0xA60F, 0xA616, 0xA612, 0xA615, 0xA61C, 0xA5F1, 0xA617, 0xA611,
         0xA60E, 0xA618, 0xA646, 0xA619, 0xA61A, 0xA610, 0xA61B, 0xA60B])

    run(0xAE2E, "$AE2E contact flag:", lambda mm, r: contact_flag(mm),
        [0xC427], until=[0xAE5C])
    run(0xACF0, "$ACF0 exchange reset:", lambda mm, r: reset_acf0(mm),
        [0xAA0D, 0xAA0B, 0xAA4B, 0xAA16, 0xAA56, 0xAA18, 0xAA58],
        until=[0xAD0B], budget=40000000)
    run(0x9CA0, "$9CA0 time tick:", lambda mm, r: time_tick(mm), [0x9CA5])
    run(0xAF27, "$AF27 rank tick:", lambda mm, r: rank_tick(mm), [0xAF34],
        budget=40000000)
    run(0xBF13, "$BF13 gfx bridge:", lambda mm, r: bf13(mm),
        [0xC41B, 0xC41C, 0xC41D, 0xC41E, 0xC41F, 0xC420, 0xC423, 0xC425,
         0xC428, 0xC429, 0xC42A, 0xC42B,
         0xC434, 0xC435, 0xC436, 0xC437, 0xC438, 0xC439, 0xC43A, 0xC43B,
         0xC42C, 0xC42D, 0xC42E, 0xC42F, 0xC430, 0xC431, 0xC432, 0xC433],
        stops=[0xC03B])

    am, at, ad = validate_ai(0xA090, ai_decide, list(range(0xA5EC, 0xA61D)))
    print(f"{'$A090 AI decide:':22} {am}/{at} match"
          f"  ({ad} deferred via $A553)")
    results.append((am, at))

    return all(m == t for m, t in results)


if __name__ == "__main__":
    main()
