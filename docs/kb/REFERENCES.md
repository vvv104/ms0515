# Reference Materials

External resources for the MS0515 emulator project.

## The plant and the machine's origin
- zx-pk.ru thread 24333, «История завода "Процессор" г. Воронеж»: https://zx-pk.ru/threads/24333-istoriya-zavoda-quot-protsessor-quot-g-voronezh/
  (read in full 2026-09-22; what it gives is in [zavod-processor.md](zavod-processor.md), its attachments in `ms0515_data/docs/zavod-processor/`)
- CodeMaster's scans of the plant's documentation: http://retro.codemaster.ru/Processor/ — the frames point at retro.hostronavt.ru, which does not answer.  The Wayback Machine crawled it once, 23-25 October 2016 (https://web.archive.org/web/20161024100032/http://retro.codemaster.ru/Processor/): the listings of `Docs`, `PCBs` and `Photos` survived whole - 127 files - but the pages were a gallery and only 27 of the files themselves came through.  Those 25 and `INDEX.md`, the list of everything that was there, are in `ms0515_data/docs/codemaster-processor/`; the `Diskette` folder was already empty when the crawler saw it.  Worth asking him for the rest

## Board scans
- NS4 board — the MS 0515 itself (revision 3A), both sides: https://radon.su/files/scan/brd/processor/NS4/
- NS5 board — the same board redrawn for the UKNC case, dated April 1990: https://radon.su/files/scan/brd/processor/NS5/
  (both fetched 2026-09-22 into `ms0515_data/scans/boards/`, ~185 MB; bare boards, no chips fitted)
- the same directory also holds NS1 (MS 0585), NS3 (the «Кондор» whose case the MS 0515 got), NS8, M5, M7, M10, K1

## CPU and instruction set
- T-11 Engineering Spec: http://www.bitsavers.org/pdf/dec/pdp11/t11/T11_Engineering_Specification_Rev_E_Mar82.pdf
- T-11 User's Manual: http://bitsavers.trailing-edge.com/pdf/dec/pdp11/t11/T11_UsersMan.pdf
- T-11 CPU model (Verilog): https://github.com/1801BM1/cpu11/tree/master/t11
- PDP-11 instruction reference: http://pdp11.org/

## Hardware datasheets
- WD1793 / FD179X FDC: http://www.bitsavers.org/components/westernDigital/FD179X-01_Data_Sheet_Oct1979.pdf
  (the MSX-side notes moved to https://hansotten.file-hunter.com/technical-info/wd1793/)
- i8253 timer (KR580VI53 clone): https://www.scs.stanford.edu/10wi-cs140/pintos/specs/8254.pdf
- i8255 PPI (KR580VV55 clone): https://www.cpcwiki.eu/imgs/f/f5/8255.pdf

## RT-11
- RT-11 documentation: http://www.bitsavers.org/pdf/dec/pdp11/rt11/

## Encoding
- KOI-8R (RFC 1489): https://datatracker.ietf.org/doc/html/rfc1489

## Emulator sources
- MAME driver: https://github.com/mamedev/mame/blob/master/src/mame/ussr/ms0515.cpp
- ms0515btl by Nikita Zimin: https://github.com/nzeemin/ms0515btl

## Community
- Emuverse wiki: https://emuverse.ru/wiki/Электроника_МС_0515
- Forum thread on the machine (zx-pk.ru): https://zx-pk.ru/threads/15146-ms-0515.html
- Forum thread on this emulator (zx-pk.ru, «ещё один эмулятор МС-0515»): https://zx-pk.ru/threads/36741
- Document scans and photos (tis.kz): https://www.tis.kz/forum/topic.php?forum=31&topic=3
