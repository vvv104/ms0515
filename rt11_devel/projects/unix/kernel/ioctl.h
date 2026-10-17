/* ioctl.h - what ioctl(fd, cmd, arg) does, for the kernel and the programs
 * alike.
 *
 * FD_WTRACK, on a diskette's device opened to write: a track written anew,
 * the image of it as the controller's WRITE TRACK takes it - gaps, syncs,
 * the ID and data fields, F5..F7 for the bytes it makes itself (fdformat
 * builds it).  The track is the device's: 0..79 on a side, fd0..fd3; on
 * fdx0, fdx1 0..159, the cylinder twice and the side, cylinder * 2 + side.
 * The kernel needs /boot/fd for it - the ROM writes no tracks - and the
 * device not in use.
 *
 * TTY_TAB, on the console: the string arg typed into the line in hand, as
 * if from the keyboard, and Tab made to hand the line to read() up to that
 * moment, a '\t' after it, the line kept in hand - till the line ends.
 * The shell's completion: it reads the line at a Tab and types the rest of
 * the word in. */
#ifndef UNIX_IOCTL_H
#define UNIX_IOCTL_H

#define FD_WTRACK 1
#define TTY_TAB 2

struct fd_track {
	unsigned track;
	const char *image;
	unsigned len;
};

#endif
