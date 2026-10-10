/* ms0515.h - the MS 0515 for a C program: the screen, the clock, the
 * keyboard, the joystick, the speaker.
 *
 * What a program on this machine does for itself, outside the monitor,
 * as docs/programming.md says it and as the ports (FIST, MANICM) do it:
 * each function names the section it comes from.  Everything is C over
 * the registers but the interrupt handlers, which need RTI and so are
 * assembler (the .s files of machine/), and the PSW's two instructions.
 *
 * A program takes what it needs with ms_*_begin() and gives it back with
 * ms_*_end() before it returns to the monitor - RT-11 is behind the
 * video window while it is open, and the keyboard's vector is the
 * ROM's.  Nothing here calls the monitor. */

#ifndef MS0515_H
#define MS0515_H

/* -- The registers ("The dispatcher, the VRAM window, the interrupts";
 *    "The keyboard"; "The screen"; "The speaker") -------------------- */

#define MS_DISPATCHER ((volatile unsigned *)0177400)
#define MS_REGISTER_C ((volatile unsigned char *)0177604)
#define MS_KEY_DATA   ((volatile unsigned char *)0177440)
#define MS_KEY_STATUS ((volatile unsigned char *)0177442)
#define MS_JOYSTICK   ((volatile unsigned char *)0177542)

/* -- The screen ("The screen") ----------------------------------------
 * Video memory seen through the window at 0100000: 200 rows of 40 words,
 * a word the attribute byte over the pixel byte, bit 7 the leftmost
 * pixel; the attribute is the Spectrum's, FLASH BRIGHT PAPER INK. */

#define MS_VRAM     ((unsigned char *)0100000)
#define MS_COLUMNS  320
#define MS_ROWS     200
#define MS_STRIDE   80				/* bytes a row */
#define ms_row(y)   (MS_VRAM + (y) * MS_STRIDE)	/* a row's first byte */

#define MS_INK(c)    (c)			/* 0 black, 1 blue, 2 red, 3 magenta, */
#define MS_PAPER(c)  ((c) << 3)			/*   4 green, 5 cyan, 6 yellow, 7 white */
#define MS_BRIGHT    0100
#define MS_FLASH     0200

/* 320x200 colour, the border black, the window open at 0100000, the
 * screen cleared to `attribute` with no pixels.  RT-11 is behind the
 * window from here: no monitor call until ms_screen_end(). */
void ms_screen_begin(unsigned char attribute);
/* The screen cleared, the window closed and the console's 640x200
 * back - black, for the console to print on. */
void ms_screen_end(void);
/* The window closed (0) or opened again (1) while the screen is on:
 * the monitor lies behind it, so a file request (rt11.h) is made with
 * it closed. */
void ms_window(int open);
/* The border's colour, 0..7 in the Spectrum's GRB order. */
void ms_border(unsigned char colour);
/* Every word of the screen: the attribute, no pixels. */
void ms_screen_clear(unsigned char attribute);
/* The pixel at (x, y) lit or put out, 0 <= x < 320, 0 <= y < 200. */
void ms_plot(int x, int y);
void ms_unplot(int x, int y);
/* The attribute of the cell (8 pixels wide, a row high) at column c of
 * row y, 0 <= c < 40. */
void ms_attribute(int c, int y, unsigned char attribute);

/* -- The clock ("The dispatcher...": RT-11 has no periodic clock here) -
 * The frame interrupt, 50 Hz on vector 100, taken and counted. */

extern volatile unsigned ms_frames;	/* frames since ms_clock_begin() */

void ms_clock_begin(void);
void ms_clock_end(void);
/* Until ms_frames has moved on by n from where it is: a pass paced. */
void ms_wait_frames(unsigned n);

/* -- The keyboard ("The keyboard") ------------------------------------
 * The MS7004's bytes taken off vector 130 into a ring (the ROM's handler
 * would hand them to RT-11); 231 sent first, keyclick off, which the
 * firmware takes as "a game runs" and repeats keys sooner.  A key sends
 * its code when pressed and again while held, never a release: a held
 * key is a timer refreshed by its code (ms_held below). */

#define MS_KEY_UP     0252
#define MS_KEY_DOWN   0251
#define MS_KEY_LEFT   0247
#define MS_KEY_RIGHT  0250
#define MS_KEY_SPACE  0324
#define MS_KEY_ENTER  0275
#define MS_KEY_SHIFT  0256
#define MS_KEY_ALL_UP 0263		/* everything released, after a modifier */
#define MS_KEY_Q 0303
#define MS_KEY_W 0315
#define MS_KEY_E 0327
#define MS_KEY_R 0335
#define MS_KEY_T 0336
#define MS_KEY_Y 0307
#define MS_KEY_U 0314
#define MS_KEY_I 0331
#define MS_KEY_O 0342
#define MS_KEY_P 0330
#define MS_KEY_A 0322
#define MS_KEY_S 0316
#define MS_KEY_D 0354
#define MS_KEY_F 0302
#define MS_KEY_G 0341
#define MS_KEY_H 0366
#define MS_KEY_J 0301
#define MS_KEY_K 0321
#define MS_KEY_L 0347
#define MS_KEY_Z 0360
#define MS_KEY_X 0343
#define MS_KEY_C 0306
#define MS_KEY_V 0362
#define MS_KEY_B 0350
#define MS_KEY_N 0334
#define MS_KEY_M 0323
#define MS_KEY_1 0300
#define MS_KEY_2 0305
#define MS_KEY_3 0313
#define MS_KEY_4 0320
#define MS_KEY_5 0325
#define MS_KEY_6 0333
#define MS_KEY_7 0340
#define MS_KEY_8 0345
#define MS_KEY_9 0352
#define MS_KEY_0 0357

void ms_keys_begin(void);
void ms_keys_end(void);
/* The next code that came, or -1. */
int ms_key(void);
/* The codes that came and were not read, dropped. */
void ms_keys_flush(void);

/* A key held: its timer, in frames, wound up by its code - longer by
 * the first, which has to outlast the keyboard's silence before the
 * repeats (125 ms in the emulator's game mode, 500 on a real MS7004),
 * than by a repeat, which comes every 50 ms. */
struct ms_held {
	unsigned char code;
	unsigned char frames;
};
#define MS_HELD_FIRST 9
#define MS_HELD_NEXT  4
/* A frame gone: every timer down by one. */
void ms_held_tick(struct ms_held *keys, int n);
/* A code came: the key it is, if any, wound up; 1 when it was one. */
int ms_held_code(struct ms_held *keys, int n, int code);
#define ms_is_held(key) ((key).frames != 0)

/* -- The joystick (port B of the MS7007 PPI, the Kempston order) ----- */

#define MS_JOY_RIGHT 1
#define MS_JOY_LEFT  2
#define MS_JOY_DOWN  4
#define MS_JOY_UP    8
#define MS_JOY_FIRE  16
/* The lines pressed, as the bits above. */
unsigned ms_joystick(void);

/* -- The speaker ("The speaker": register C bit 6, the Spectrum's OUT) - */

/* The speaker line flipped: a click; a square wave by flipping in time. */
void ms_speaker_flip(void);

/* -- The processor ----------------------------------------------------
 * The PSW's priority raised to 7 and put back, round a few instructions
 * an interrupt must not split (the swapping of a vector). */

unsigned ms_interrupts_off(void);		/* the PSW before */
void ms_interrupts_restore(unsigned psw);

#endif
