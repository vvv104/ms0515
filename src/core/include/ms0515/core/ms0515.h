/*
 * ms0515.h — Unified public API header for the MS0515 emulation core
 *
 * Include this single header to get access to all core types and functions.
 *
 * The Elektronika MS 0515 is a Soviet personal computer manufactured by
 * the "Processor" company in Voronezh, USSR.  It is based on the
 * KR1807VM1 processor (a clone of the DEC T-11, implementing a subset
 * of the PDP-11 instruction set).
 *
 * Key specifications:
 *   CPU:    KR1807VM1 @ 7.5 MHz (PDP-11 compatible, 66 instructions)
 *   RAM:    128 KB (banked, 16 × 8 KB)
 *   ROM:    16 KB (2 × K573RF4B UV-EPROM)
 *   Video:  320×200 / 8 colors (attribute mode, ZX Spectrum-like)
 *           640×200 / monochrome
 *   VRAM:   16 KB (shared with RAM, behind ROM shadow)
 *   FDD:    KR1818VG93 (WD1793 clone), 5.25" QD, up to 4 drives
 *   Timer:  KR580VI53 (Intel 8253 clone), 3 channels @ 2 MHz
 *   Serial: KR580VV51 (Intel 8251 clone) — keyboard + printer
 *   PPI:    KR580VV55 (Intel 8255 clone) — system registers A/B/C
 *   Sound:  1-bit speaker, gated by PIT channel 2
 *
 * Sources:
 *   - https://ru.wikipedia.org/wiki/Электроника_МС_0515
 *   - NS4 technical description (3.858.420 TO)
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 *   - PDP-11 Architecture Handbook (DEC)
 *   - T-11 User's Guide (EK-DCT11-UG)
 */

#ifndef MS0515_H
#define MS0515_H

#include "cpu.h"
#include "memory.h"
#include "timer.h"
#include "keyboard.h"
#include "floppy.h"
#include "board.h"

#endif /* MS0515_H */
