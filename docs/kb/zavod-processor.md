# The plant, the board and who made the machine

What the МС 0515 is on the factory's own terms, read out of the zx-pk.ru
thread «История завода "Процессор" г. Воронеж» (thread 24333, 20 pages,
191 posts, read in full on 2026-09-22).  The thread's attachments, an
interview with a plant engineer among them, are kept in
`ms0515_data/docs/zavod-processor/`, which also holds the quotes in full.

## The machine is board НС4, and it was called УБПК-16

The plant numbered its motherboards НС1..НС8.  A participant sets them out:

| board | machine | when | note |
|---|---|---|---|
| НС1 | МС 0585 (Электроника 85) | ~1984-85 | a Pro 350; large series, 8 board revisions |
| НС2 | МС 0508 (Электроника 85.1) | | a Pro 380; ~10 prototypes |
| НС3 | НИР «Кондор» | ~1988 | no western original; never made — **but its case became the МС 0515's** |
| **НС4** | **МС 0515 (УБПК-16)** | **~1990** | no western original; a very small series; **4 board revisions are known** |
| НС5 | some МС 0515 | | НС4 redrawn for the УКНЦ case |
| НС7 | МС 0532, МС 0907 | ~1993 | a variation on НС4 |
| НС8 | unknown | ~1996 | a variation on НС4; one board of the first revision survives |

So «УБПК» and «УПБК-16», which the kits' programs keep addressing
(«Самые лучшие драйверы для УПБК!!!», «АДАПТАЦИЯ ДЛЯ УБПК», «ПРОГРАММА
ФОРМАТИРОВАНИЯ ДИСКЕТ ДЛЯ УПБК-16»), is this machine under its factory
name.  The forum's own engineers say it plainly - «УБПК это и есть МС 0515»
- and add which software went with which name: «на МС 0515, операционка
ОСА-1, на УБПК ОС-16 и ПРОС (Д)».  ОС-16 is the monitor of the kit the
collection calls `mihin`.

Scans of the НС4 and НС5 boards are on radon.su (see `REFERENCES.md`).

## Who made it

The plant's veteran, asked who led which block, answers for ours:

> НС4 - УПБК ? Разработка + ОС и т.д. - **Понимаш Владимир** - оригинальная,
> взял за основу Sincler, поставил процессор T11, видеопамять по другому
> организовал.  Для УПБК оригинальные вещи не пошедшие в серию -
> **видеоввод, жесткий диск AT**.

This is the plant's own confirmation of what the collection had worked out
from the diskettes: the chief designer is Понимаш Владимир Анатольевич of
НИПП «Омега», Lvov, and the operating system was his too.  Two things are
new:

* the machine was begun **from the Sinclair** as a model, with a T11
  processor put in it and the video memory organised differently;
* a **video input** and an **AT hard disk** were developed for it and never
  went into the series.  (What the emulator has is a paravirtual disk of its
  own making, not that one - see `docs/folder-device.md` and `hd.h`.)

The plant made no chips itself: they came from Minsk, Baku, Zelenograd and
from ВЗПП across town.

## НПФ «Сенсор» - still unidentified

The programs of the `mihin` kit name a Voronezh firm twice, as СПФ and as
НПФ «Сенсор», always beside Михин Ю.А.  In this thread somebody asks the
same question we did:

> Что за организация НПФ "Сенсор", и как она связана с "Процессором"?  Они
> разрабатывали ПО на Э85 для игр КВН 90-го года и драйверы для УБПК-16
> (МС 0515).

and nobody answers.  New from that question: «Сенсор» also wrote software
for the Электроника 85 - the КВН games of 1990.  That the firm was a part of
the plant remains unproved; outside these programs it is named nowhere.

## What could not be fetched

`retro.codemaster.ru/Processor/` - the scanned documentation, boards and
photographs collected by CodeMaster (he worked in the plant's ОКБ from 1986
to 1993, in department 62, systems programming, and he is the one who sends
this emulator's bug reports from the forum).  Its frames point at
`retro.hostronavt.ru`, which does not answer, and the Wayback Machine has no
copy.  Worth asking him for a mirror.
