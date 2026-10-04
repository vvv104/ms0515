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

FUNCTION RND:REAL;
FUNCTION RAN(VAR SEED1,SEED2:INTEGER):REAL;FORTRAN;
 BEGIN
 RND:=RAN(SEED1,SEED2)
 END;

PROCEDURE PAUSE;
BEGIN
 KBMODE(1);
 REPEAT UNTIL INKEY<>0;
 KBMODE(0)
END;

PROCEDURE CHECK(CONC:INTEGER;FILNAM:NAME;VAR S:TOFSPRT);
VAR N,I,J:INTEGER;
    F:FILE OF WORD;
    CHECKSUM,CRC ORIGIN 1000B : WORD;
    WZLOM:BOOLEAN;
BEGIN
 RESET(F,FILNAM,'SAV',N);
 IF N<=0 THEN
  BEGIN
   WRITELN('File not found DK:',FILNAM:1,'.SAV');
   {$C
      .RADIX 8
      EMT 350
      .RADIX 10
   }
  END;
 N:=N-TRUNC((MAXNUMSPR+1)/32)-1;
 CHECKSUM:=0;
 FOR I:=1 TO N DO
  FOR J:=1 TO 256 DO
   BEGIN
    CHECKSUM:=CHECKSUM+F^;
    GET(F)
   END;
 CRC:=CHECKSUM;
 WZLOM:=(CRC<>F^);
 IF WZLOM AND (CONC=1) THEN 
  BEGIN
   WRITELN('Don''t fuck my mind !');
   {$C
      .RADIX 8
      EMT 350
      .RADIX 10
   }
  END;
 FOR I:=1 TO 8 DO
  GET(F);
  FOR J:=0 TO MAXNUMSPR DO
   FOR I:=0 TO 7 DO
    BEGIN
     S[J,I]:=F^;
     IF NOT EOF(F) THEN GET(F)
    END;
 CLOSE(F)
END;

PROCEDURE Setspr(x,y:integer;VAR S:SPRT;MODE:INTEGER);
  const
        viRam = 40000B;
  type
        pointer = ^ vektor;
        vektor = array[1..8000] of WORD;

  var
        d     origin viRam  : vektor;
        regdp origin 177400B,
        DpRam origin 157700B: integer;
        state,I,ADR:INTEGER;
        uk      : pointer;
        T       : SPRT;
BEGIN
   uk := @d;
   T := S;
   adr := 40*(y*8)+x+1;
   state := DPRAM;
   DPRAM:=200B OR DPRAM;
   RegDp :=DPRAM;
{$A-}
   FOR I:=0 TO 7 DO
    begin
     CASE MODE OF
      2:UK^[ADR]:=NOT UK^[ADR];
      1:uk^[adr]:=T[I] OR uk^[adr];
      0:uk^[adr]:=T[I]
     END;
     adr := adr+40
    END;
{$A+}
   DPRAM:=STATE;
   RegDp := DPRAM
end;
PROCEDURE PUTCUR(X,Y:INTEGER);
BEGIN
 SETSPR(XMIN+X-1,YMIN+Y-1,S[11],1)
END;

PROCEDURE RESCUR(X,Y:INTEGER);
VAR SC:INTEGER;
BEGIN
 IF (A[X,Y]>=0) AND (A[X,Y]<=9) THEN SC:=12
 ELSE
 IF (A[X,Y]>=10) AND (A[X,Y]<=18) THEN SC:=A[X,Y]-10
 ELSE
 IF (A[X,Y]>=19) AND (A[X,Y]<=28) THEN SC:=13;
 SETSPR(XMIN+X-1,YMIN+Y-1,S[SC],0)
END;
