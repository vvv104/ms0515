/* ioctl.h - what ioctl(fd, cmd, arg) does, for the kernel and the programs
 * alike.
 *
 * FD_WTRACK, on a diskette's device opened to write: a track written anew,
 * the image of it as the controller's WRITE TRACK takes it - gaps, syncs,
 * the ID and data fields, F5..F7 for the bytes it makes itself (fdformat
 * builds it).  The track is the device's: 0..79 on a side, fd0..fd3; on
 * fdx0, fdx1 0..159, the cylinder twice and the side, cylinder * 2 + side.
 * The kernel needs /boot/fd for it - the ROM writes no tracks - and the
 * device not in use. */
#ifndef UNIX_IOCTL_H
#define UNIX_IOCTL_H

#define FD_WTRACK 1

struct fd_track {
	unsigned track;
	const char *image;
	unsigned len;
};

#endif
