{$E+}
   CONST
	MAXNUMSPR=62;
   TYPE
	WORD=0..65535;
	ZNKP=-1..28;
	POLE=ARRAY[0..41,0..21] OF ZNKP;
	SPRT=ARRAY[0..7] OF WORD;
	TOFSPRT=ARRAY[0..MAXNUMSPR] OF SPRT;
	NAME=ARRAY[1..6] OF CHAR;
   VAR
	NEWP,MARK,QUIT:BOOLEAN;
        S:TOFSPRT;
	A:POLE;
        FILNAME:NAME;
        PGP,NGP,KM:INTEGER;
	KPPM,KNPM,KOK:INTEGER;
        XMIN,XMAX,YMIN,YMAX,X,Y:INTEGER;
	SEED1,SEED2,I,J,K,N,XP,YP:INTEGER;

Function  Inkey:integer;				EXTERNAL;
Procedure KbMode(mode:integer);				EXTERNAL;
Procedure GotoXY       (x, y: integer);			EXTERNAL;
Procedure CURON;					EXTERNAL;
Procedure CUROFF;					EXTERNAL;
Procedure ClrScr;					EXTERNAL;
FUNCTION RND:REAL;					EXTERNAL;
PROCEDURE PAUSE;					EXTERNAL;
PROCEDURE SETSPR(X,Y:INTEGER;VAR S:SPRT;MODE:INTEGER);	EXTERNAL;
PROCEDURE PUTCUR(X,Y:INTEGER);				EXTERNAL;
PROCEDURE RESCUR(X,Y:INTEGER);				EXTERNAL;

PROCEDURE SETPOLE(NEWP:BOOLEAN;PGP,NGP,KM:INTEGER);
PROCEDURE HORIZLINE(X1,X2,Y,MODE:INTEGER);		EXTERNAL;
PROCEDURE VERTICALLINE(X,Y1,Y2,MODE:INTEGER);		EXTERNAL;
PROCEDURE RANDOMIZE;
 VAR T:REAL;
BEGIN
 T:=TIME;
 SEED1:=TRUNC((T-TRUNC(T))*32767);
 SEED2:=32767 MOD SEED1
END;

BEGIN
 CLRSCR;
 RANDOMIZE;
 IF NEWP THEN
  BEGIN
   GOTOXY(36,12);
   WRITE('Wait...');
   FOR I:=1 TO PGP DO
    FOR J:=1 TO NGP DO
     A[I,J]:=0;
   FOR K:=1 TO KM DO
    BEGIN
     REPEAT
      XP:=TRUNC(RND*PGP)+1;
      YP:=TRUNC(RND*NGP)+1;
     UNTIL A[XP,YP]<>9;
     A[XP,YP]:=9;
     FOR I:=XP-1 TO XP+1 DO
      FOR J:=YP-1 TO YP+1 DO
       IF (A[I,J]<>9) THEN A[I,J]:=A[I,J]+1
    END;
   FOR I:=0 TO PGP+1 DO
    BEGIN
     A[I,0]:=-1;
     A[I,NGP+1]:=-1
    END;
   FOR J:=0 TO NGP+1 DO
    BEGIN
     A[0,J]:=-1;
     A[PGP+1,J]:=-1
    END;
{ õÓÔÁÎÏ×ËÁ ÈÁÒÁËÔÅÒÉÓÔÉË ÎÏ×ÏÇÏ ÐÏÌÑ }
   KOK:=0;
   KPPM:=0;
   KNPM:=0;
   XMIN:=(40-PGP) DIV 2;
   YMIN:=4;
   XMAX:=XMIN+PGP-1;
   YMAX:=YMIN+NGP-1;
   X:=1;
   Y:=1;
{ óÔÉÒÁÅÔ ÚÁÐÉÓØ "Wait..." }
   GOTOXY(36,12);
   WRITE('       ')
  END;
{ òÉÓÕÅÔ ÎÁ ÜËÒÁÎÅ ÐÏÌÅ }
  FOR J:=0 TO YMAX+1 DO
   FOR I:=XMIN-1 TO XMAX+1 DO
    BEGIN
     IF J=0 THEN
      BEGIN
       N:=19;
       IF I=XMIN-1 THEN N:=18;
       IF I=XMAX+1 THEN N:=20
      END
     ELSE
     IF J=YMIN-1 THEN
      BEGIN
       N:=19;
       IF I=XMIN-1 THEN N:=23;
       IF I=XMAX+1 THEN N:=24
      END
     ELSE
     IF J=YMAX+1 THEN
      BEGIN
       N:=19;
       IF I=XMIN-1 THEN N:=22;
       IF I=XMAX+1 THEN N:=21
      END
     ELSE
      BEGIN
       IF (J>=YMIN) AND (J<=YMAX) THEN 
        BEGIN
         K:=A[I-XMIN+1,J-YMIN+1];
         IF (K>=0) AND (K<=9) THEN N:=12
         ELSE
         IF (K>=10) AND (K<=18) THEN N:=K-10
         ELSE
         IF (K>=19) AND (K<=28) THEN N:=13
        END
       ELSE N:=16;
       IF I=XMIN-1 THEN N:=17;
       IF I=XMAX+1 THEN N:=17
      END;
      SETSPR(I,J,S[N],0)
    END;
  HORIZLINE((XMIN-1)*16-1,(XMAX+2)*16-1,0,1);
  VERTICALLINE((XMIN-1)*16-1,0,(YMAX+2)*8-1,1);
  VERTICALLINE((XMIN-1)*16,0,(YMAX+2)*8-1,1);
 PUTCUR(X,Y)
END;

PROCEDURE HELP(FILNAM:NAME);
 TYPE
  LINE=PACKED ARRAY[1..80] OF CHAR;
  TXT=PACKED ARRAY[1..46] OF LINE;
 VAR
  F:FILE OF CHAR;
  C:CHAR;
  K,N,I,J:INTEGER;
  T:TXT;
  B:BOOLEAN;
BEGIN
 CLRSCR;
 RESET(F,FILNAM,'HLP',N);
 IF N<=0 THEN
  BEGIN
   WRITELN('File not found ',FILNAM,'.HLP');
   PAUSE;
   KBMODE(1)
  END
 ELSE
  BEGIN
   GOTOXY(36,12);
   WRITE('Wait...');
   C:=F^;
   GET(F);
   SEED1:=ORD(C);  
   T[1,1]:=C;
   C:=F^;
   GET(F);
   SEED2:=ORD(C);
   T[1,2]:=C;
   I:=1;
   K:=3;
   FOR I:=1 TO 46 DO
    BEGIN
     FOR J:=K TO 80 DO
      BEGIN
       N:=TRUNC(RND*256);
       C:=CHR(ORD(F^)-N);
       T[I,J]:=C;
       GET(F)
      END;
     K:=1
    END;
   CLOSE(F);
   CLRSCR;
   FOR J:=1 TO 23 DO
    WRITE(T[J]);
   WRITELN;
   WRITE(' îÁÖÍÉÔÅ ¸ É ¹ ÄÌÑ ÐÒÏÓÍÏÔÒÁ ÐÏÍÏÝÉ ÉÌÉ ÌÀÂÕÀ ÄÒÕÇÕÀ ËÌÁ×ÉÛÕ ÄÌÑ ×ÙÈÏÄÁ × ÉÇÒÕ.');
   I:=1;
   REPEAT
    B:=TRUE;
    CASE INKEY OF
     27:CASE INKEY OF
        65:BEGIN
            B:=FALSE;
            IF I=2 THEN
             BEGIN
              I:=1;
              CLRSCR;
              FOR J:=1 TO 23 DO
               WRITE(T[J]);
              WRITELN;
              WRITE(' îÁÖÍÉÔÅ ¸ É ¹ ÄÌÑ ÐÒÏÓÍÏÔÒÁ ÐÏÍÏÝÉ ÉÌÉ ÌÀÂÕÀ ÄÒÕÇÕÀ ËÌÁ×ÉÛÕ ÄÌÑ ×ÙÈÏÄÁ × ÉÇÒÕ.')
             END
            END;
        66:BEGIN
            B:=FALSE;
            IF I=1 THEN
             BEGIN
              I:=2;
              CLRSCR;
              FOR J:=24 TO 46 DO
               WRITE(T[J]);
              WRITELN;
              WRITE(' îÁÖÍÉÔÅ ¸ É ¹ ÄÌÑ ÐÒÏÓÍÏÔÒÁ ÐÏÍÏÝÉ ÉÌÉ ÌÀÂÕÀ ÄÒÕÇÕÀ ËÌÁ×ÉÛÕ ÄÌÑ ×ÙÈÏÄÁ × ÉÇÒÕ.')
             END
            END
       END
    END
   UNTIL B
  END;
 SETPOLE(FALSE,PGP,NGP,KM)
END;

PROCEDURE NEWPOLE;
BEGIN
 SETPOLE(TRUE,PGP,NGP,KM)
END;

PROCEDURE BEGINNER;
BEGIN
 PGP:=8;
 NGP:=8;
 KM:=10;
 SETPOLE(TRUE,PGP,NGP,KM)
END;

PROCEDURE INTERMEDIATE;
BEGIN
 PGP:=16;
 NGP:=16;
 KM:=40;
 SETPOLE(TRUE,PGP,NGP,KM)
END;

PROCEDURE EXPERT;
BEGIN
 PGP:=30;
 NGP:=16;
 KM:=99;
 SETPOLE(TRUE,PGP,NGP,KM)
END;

PROCEDURE CUSTOM;
TYPE CIF=0..9;
VAR POS,K:INTEGER;
    N:ARRAY[1..7] OF CIF;
BEGIN
 CLRSCR;
 GOTOXY(13,7);
 WRITE(' ¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¡');
 GOTOXY(13,8);
 WRITE('¥                               ¥');
 GOTOXY(13,9);
 WRITE('¥ úÁÄÁÊÔÅ ÒÁÚÍÅÒÙ ÉÇÒÏ×ÏÇÏ ÐÏÌÑ ¥');
 GOTOXY(13,10);
 WRITE('¥                               ¥');
 GOTOXY(13,11);
 WRITE('¥            ›› X ››            ¥');
 GOTOXY(13,12);
 WRITE('¥                               ¥');
 GOTOXY(13,13);
 WRITE('¥       É ËÏÌÉÞÅÓÔ×Ï ÍÉÎ        ¥');
 GOTOXY(13,14);
 WRITE('¥                               ¥');
 GOTOXY(13,15);
 WRITE('¥              ›››              ¥');
 GOTOXY(13,16);
 WRITE('¥                               ¥');
 GOTOXY(13,17);
 WRITE('£¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¤¢');
 CURON;
 GOTOXY(26,11);
 POS:=1;
 REPEAT
  I:=INKEY;
  K:=POS;
  CASE POS OF
   1:IF (I>=48) AND (I<=50) THEN
      BEGIN
       WRITE(I-48:1);
       N[1]:=I-48;
       POS:=2
      END;
   2:IF N[1]<>2 THEN
      BEGIN
       IF (I>=48) AND (I<=50) THEN
        BEGIN
         WRITE(I-48:1);
         GOTOXY(31,11);
         N[2]:=I-48;
         POS:=3
        END
      END
     ELSE
      IF I=48 THEN
       BEGIN
        WRITE(0:1);
        GOTOXY(31,11);
        N[2]:=0;
        POS:=3
       END;
   3:IF (I>=48) AND (I<=51) THEN
      BEGIN
       WRITE(I-48:1);
       N[3]:=I-48;
       POS:=4
      END;
   4:IF N[3]<>3 THEN
      BEGIN
       IF (I>=48) AND (I<=50) THEN
        BEGIN
         WRITE(I-48:1);
         GOTOXY(28,12);
         N[4]:=I-48;
         POS:=5
        END
      END
     ELSE
      IF (I>=48) AND (I<=54) THEN
       BEGIN
        WRITE(I-48:1);
        GOTOXY(28,12);
        N[2]:=I-48;
        POS:=5
       END;
   5:IF ( ((I-48)*100)>=TRUNC((N[1]*10+N[2])*(N[3]*10+N[4])/32) ) AND
        ( ((I-48)*100)<=TRUNC(31*(N[1]*10+N[2])*(N[3]*10+N[4])/32) ) THEN
      BEGIN
       N[5]:=I-48;
       WRITE(N[5]:1);
       POS:=6
      END;
   6:IF ( ((I-48)*10+N[5]*100)>=TRUNC((N[1]*10+N[2])*(N[3]*10+N[4])/32) ) AND
     ( ((I-48)*10+N[5]*100)<=TRUNC(31*(N[1]*10+N[2])*(N[3]*10+N[4])/32) ) THEN
      BEGIN
       N[6]:=I-48;
       WRITE(N[6]:1);
       POS:=7
      END;
   7:IF ( (I-48+N[5]*100+N[6]*10)>=TRUNC((N[1]*10+N[2])*(N[3]*10+N[4])/32) ) AND
        ( (I-48+N[5]*100+N[6]*10)<=TRUNC(31*(N[1]*10+N[2])*(N[3]*10+N[4])/32) ) THEN
      BEGIN
       N[7]:=I-48;
       WRITE(N[7]:1);
       POS:=8
      END
  END;
  IF K=POS THEN WRITE(CHR(7))
 UNTIL POS=8;
 PGP:=N[3]*10+N[4];
 NGP:=N[1]*10+N[2];
 KM:=N[5]*100+N[6]*10+N[7];
 CUROFF;
 SETPOLE(TRUE,PGP,NGP,KM)
END;

PROCEDURE BESTTIMES;
BEGIN
END;

PROCEDURE VERSION(A:BOOLEAN);
BEGIN
 CLRSCR;
 writeln('   The Minesweeper Game, Copyright (C) 1995 Ver. 1.01.   All rights reserved.');
 WRITELN('          Production by Voronkov Software. Russia, Voronezh 223518.');
 WRITELN('                  Software for PC MS 0515 or compatible');
 IF A THEN 
  BEGIN
   PAUSE;
   KBMODE(1);
   SETPOLE(FALSE,PGP,NGP,KM)
  END
END;

PROCEDURE MENU;
VAR
  EX:BOOLEAN;
  POS,I:INTEGER;
BEGIN
 GOTOXY(30,0);
 WRITE('””””””””””””””””””‘');
 GOTOXY(30,1);
 WRITE('•                  •');
 GOTOXY(30,2);
 WRITE('• Help         F1  •');
 GOTOXY(30,3);
 WRITE('• New          F2  •');
 GOTOXY(30,4);
 WRITE('• Beginner     F3  •');
 GOTOXY(30,5);
 WRITE('• Intermediate F4  •');
 GOTOXY(30,6);
 WRITE('• Expert       F5  •');
 GOTOXY(30,7);
 WRITE('• Custom       F6  •');
 GOTOXY(30,8);
 WRITE('• Marker(?) ');
 IF MARK THEN WRITE('on ') ELSE WRITE('off');
 WRITE('    •');
 GOTOXY(30,9);
 WRITE('• Best Times   F8  •');
 GOTOXY(30,10);
 WRITE('• Exit         F10 •');
 GOTOXY(30,11);
 WRITE('• Version          •');
 GOTOXY(30,12);
 WRITE('“””””””””””””””””””’');
 POS:=0;
 EX:=FALSE;
 FOR I:=1 TO 8 DO
  SETSPR(I+15,POS+1,S[0],2);
 REPEAT
 CASE INKEY OF
  10:BEGIN
     EX:=TRUE;
     CASE POS OF
      0:SETPOLE(FALSE,PGP,NGP,KM);
      1:HELP(FILNAME);
      2:NEWPOLE;
      3:BEGINNER;
      4:INTERMEDIATE;
      5:EXPERT;
      6:CUSTOM;
      7:BEGIN
         MARK:=NOT MARK;
         GOTOXY(30,8);
         WRITE('• Marker(?) ');
         IF MARK THEN WRITE('on ') ELSE WRITE('off');
         WRITE('    •');
         SETPOLE(FALSE,PGP,NGP,KM)
        END;
      8:BESTTIMES;
      9:QUIT:=TRUE;
     10:VERSION(TRUE)
     END
     END;
  27:BEGIN
     EX:=TRUE;
     CASE INKEY OF
      65:BEGIN
          FOR I:=1 TO 8 DO
           SETSPR(I+15,POS+1,S[0],2);          
          IF POS=0 THEN POS:=10 ELSE POS:=POS-1;
          FOR I:=1 TO 8 DO
           SETSPR(I+15,POS+1,S[0],2);
          EX:=FALSE
         END;
      66:BEGIN
          FOR I:=1 TO 8 DO
           SETSPR(I+15,POS+1,S[0],2);          
          IF POS=10 THEN POS:=0 ELSE POS:=POS+1;
          FOR I:=1 TO 8 DO
           SETSPR(I+15,POS+1,S[0],2);
          EX:=FALSE
         END;
      69:HELP(FILNAME);
      70:NEWPOLE;
      71:BEGINNER;
      72:INTERMEDIATE;
      73:EXPERT;
      74:CUSTOM;
      76:BESTTIMES;
      78:QUIT:=TRUE
     END
    END
 END
 UNTIL EX
END;

PROCEDURE UP;
BEGIN
 IF Y>1 THEN 
  BEGIN
   RESCUR(X,Y);
   Y:=Y-1;
   PUTCUR(X,Y)
  END
END;

PROCEDURE DOWN;
BEGIN
 IF Y<NGP THEN 
  BEGIN
   RESCUR(X,Y);
   Y:=Y+1;
   PUTCUR(X,Y)
  END
END;

PROCEDURE RIGHT;
BEGIN
 IF X<PGP THEN 
  BEGIN
   RESCUR(X,Y);
   X:=X+1;
   PUTCUR(X,Y)
  END
END;

PROCEDURE LEFT;
BEGIN
 IF X>1 THEN 
  BEGIN
   RESCUR(X,Y);
   X:=X-1;
   PUTCUR(X,Y)
  END
END;

PROCEDURE VZRYV(X,Y:INTEGER);
 VAR I,J:INTEGER;
BEGIN
 FOR I:=1 TO PGP DO
  FOR J:=1 TO NGP DO
   IF A[I,J]=9 THEN 
    SETSPR(XMIN+I-1,YMIN+J-1,S[9],0)
   ELSE
   IF (A[I,J]>=20) AND (A[I,J]<=28) THEN
    SETSPR(XMIN+I-1,YMIN+J-1,S[14],1);    
 IF X+Y<>0 THEN
  SETSPR(XMIN+X-1,YMIN+Y-1,S[10],0);    
 QUIT:=TRUE;
 PAUSE
END;

PROCEDURE POBEDA;
VAR I,J:INTEGER;
BEGIN
 FOR I:=1 TO PGP DO
  FOR J:=1 TO NGP DO
   IF A[I,J]=9 THEN 
    SETSPR(XMIN+I-1,YMIN+J-1,S[13],0)
   ELSE
   IF (A[I,J]>0) AND (A[I,J]<9) THEN
    SETSPR(XMIN+I-1,YMIN+J-1,S[A[I,J]],0);
  QUIT:=TRUE;
  PAUSE
END;

PROCEDURE OTKKL(X,Y:INTEGER);
 PROCEDURE OTKRPROST(X,Y:INTEGER);
  VAR I,J:INTEGER;
 BEGIN
  A[X,Y]:=10;
  SETSPR(XMIN+X-1,YMIN+Y-1,S[0],0);
  KOK:=KOK+1;
  FOR I:=X-1 TO X+1 DO
   FOR J:=Y-1 TO Y+1 DO
    IF A[I,J]=0 THEN OTKRPROST(I,J)
    ELSE IF (A[I,J]>0) AND (A[I,J]<9) THEN
    BEGIN
     SETSPR(XMIN+I-1,YMIN+J-1,S[A[I,J]],0);
     A[I,J]:=A[I,J]+10;
     KOK:=KOK+1
    END
 END;
BEGIN
 IF A[X,Y]=0 THEN OTKRPROST(X,Y)
 ELSE
 IF (A[X,Y]>0) AND (A[X,Y]<9) THEN
  BEGIN
   SETSPR(XMIN+X-1,YMIN+Y-1,S[A[X,Y]],0);
   A[X,Y]:=A[X,Y]+10;
   KOK:=KOK+1
  END
 ELSE
 IF A[X,Y]=9 THEN VZRYV(X,Y);
 IF KOK=PGP*NGP-KM THEN POBEDA ELSE PUTCUR(X,Y)
END;

PROCEDURE POMMIN(X,Y:INTEGER);
BEGIN
 IF A[X,Y]<=9 THEN 
  BEGIN
   IF A[X,Y]=9
   THEN
    BEGIN
     A[X,Y]:=19;
     KPPM:=KPPM+1
    END
   ELSE
    BEGIN
     A[X,Y]:=A[X,Y]+20;
     KNPM:=KNPM+1
    END;
    SETSPR(XMIN+X-1,YMIN+Y-1,S[13],0)
  END
 ELSE
 IF A[X,Y]>=19 THEN 
  BEGIN
   IF A[X,Y]=19
    THEN
     BEGIN
      A[X,Y]:=9;
      KPPM:=KPPM-1
     END
    ELSE
     BEGIN
      A[X,Y]:=A[X,Y]-20;
      KNPM:=KNPM-1
     END;
    SETSPR(XMIN+X-1,YMIN+Y-1,S[12],0)
  END;
  IF (KPPM+KNPM=KM) THEN
   IF (KNPM=0) THEN POBEDA
    ELSE VZRYV(0,0)
     ELSE PUTCUR(X,Y)
END;
