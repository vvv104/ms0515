"""How long a delay loop takes on this machine - in one place.

The beeper and the demo mains keep time the way the original does: by
counting turns of a busy loop rather than by watching a clock.  The
original counts Z80 T-states; the port counts turns of

    54$:    SOB     Rn,54$

so every such constant is a conversion, and all of them live here.

The Spectrum's Z80 runs at 3.5 MHz and this machine's KR1807VM1 at 7.5, so
one T-state is 7.5/3.5 processor clocks.  A SOB turn is 18 clocks - six
microcycles of 400 ns, from the T-11 tables (see `core/src/cpu_ops.c` and
`core/tests/test_cpu_timing.cpp`).

Until v1.6.0 the emulator charged SOB nine clocks instead of eighteen, and
every constant in this port was calibrated by ear against that.  When the
processor started taking the time its documentation gives, the tune played
at half speed - which is what a delay calibrated against a machine running
at double speed does.  Hence this module: the next time either machine's
figures are corrected it is one edit, not a hunt through the generators.
"""

CPU_HZ = 7_500_000                  # KR1807VM1 on the MS-0515
Z80_HZ = 3_500_000                  # the Spectrum the game came from
CLOCKS_PER_T = CPU_HZ / Z80_HZ      # 2.142857...
SOB_CLOCKS = 18                     # T-11: six microcycles of 400 ns


def turns_for_t(t_states):
    """SOB turns that take as long as `t_states` of the original."""
    return max(1, round(t_states * CLOCKS_PER_T / SOB_CLOCKS))


def turns_for_ms(ms):
    """SOB turns that take `ms` milliseconds on this machine."""
    return max(1, round(ms * CPU_HZ / 1000.0 / SOB_CLOCKS))


def split(turns, outer):
    """`turns` as an (outer, inner) pair for a nested pair of SOB loops.

    A single SOB counts at most 65536 turns, and the long waits need more.
    """
    return outer, max(1, round(turns / outer))
