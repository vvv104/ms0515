/ machine.s - the MS 0515 under STARS: the screen, the clock, the keyboard.
/
/ The way MANICM's MS0515.MAC does it (docs/programming.md):
/   - register C (177604, write-only): 040 is 320x200 colour, black
/     border, speaker low; 010 the console's 640x200 again on exit;
/   - the dispatcher (177400): 07377 is the frame interrupt on and the
/     VRAM window at 100000..137777 - RT-11 is behind it, so no monitor
/     call while the program runs; 02177 as the ROM left it, on exit;
/   - the frame interrupt (vector 100, 50 Hz) counts frames for the
/     program's pace;
/   - the keyboard's bytes (vector 130) go into a ring: the ROM's handler
/     would hand them to RT-11 otherwise.  231 sent to the keyboard
/     first - keyclick off, which the firmware takes as "a game runs"
/     and repeats keys sooner.
/ Both handlers are below the window with their vectors, so interrupts
/ stay on.

	.globl	_hw_begin, _hw_end, _frames, _kbring, _kbhead

SYSC	= 0177604
DISPAT	= 0177400
KBDATA	= 0177440
KBSTAT	= 0177442
FRVEC	= 0100
KBVEC	= 0130

	.text

/ void hw_begin(void)
_hw_begin:
	mov	*$KBVEC, kbsave
	mov	*$KBVEC+2, kbsave+2
	mov	$kbisr, *$KBVEC
	mov	$0340, *$KBVEC+2
	clr	_kbhead
1:	movb	*$KBSTAT, r0		/ until the USART can send
	bit	$1, r0
	beq	1b
	movb	$0231, *$KBDATA		/ keyclick off: the game repeat
	mov	*$FRVEC, frsave
	mov	*$FRVEC+2, frsave+2
	mov	$frisr, *$FRVEC
	mov	$0340, *$FRVEC+2
	clr	_frames
	movb	$040, *$SYSC		/ 320x200 colour, black border, speaker low
	mov	$07377, *$DISPAT	/ the frame interrupt on, the window open
	rts	pc

/ void hw_end(void)
_hw_end:
	movb	$010, *$SYSC		/ the console's 640x200, black border
	mov	$02177, *$DISPAT	/ the window off, the frame interrupt off
	mov	kbsave, *$KBVEC
	mov	kbsave+2, *$KBVEC+2
	mov	frsave, *$FRVEC
	mov	frsave+2, *$FRVEC+2
	rts	pc

frisr:
	inc	_frames
	rti

kbisr:
	mov	r0, -(sp)
	mov	r1, -(sp)
	mov	_kbhead, r1
	movb	*$KBDATA, _kbring(r1)
	inc	r1
	bic	$0177760, r1		/ sixteen bytes round
	mov	r1, _kbhead
	mov	(sp)+, r1
	mov	(sp)+, r0
	rti

	.data
_frames:
	.word	0
_kbhead:
	.word	0
_kbring:
	.word	0, 0, 0, 0, 0, 0, 0, 0
kbsave:
	.word	0, 0
frsave:
	.word	0, 0
