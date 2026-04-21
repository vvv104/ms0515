# Known Issues

## ms0515-roma-original.rom + omega-lang.dsk — pink screen, tight loop

- **ROM**: ms0515-roma-original.rom (original unpatched ROM-A)
- **Disk**: omega-lang.dsk (Omega — language disk)
- **Symptom**: Screen fills with pink background, CPU enters tight loop.
  The patched ROM-A (`ms0515-roma.rom`) boots this disk successfully.
- **Likely cause**: Original ROM-A initialises video or memory differently,
  causing the Omega loader to write to wrong addresses or misconfigure
  the display mode.

## mihin.dsk — RUS/LAT switch interpreted as ^O

- **ROM**: all ROMs
- **Disk**: mihin.dsk
- **Symptom**: Switching from RUS to LAT mode is interpreted by the OS
  as Ctrl+O (^O) followed by a carriage return.  After that the system
  stops responding to key presses until the user switches back to RUS
  and then to LAT again.
- **Likely cause**: The RUSLAT toggle scancode or its timing is being
  misinterpreted by the keyboard driver as a control character sequence.

## TODO: FDC synchronous command execution (busy delay hack)

- **File**: `emu/core/src/floppy.c`, `finish_command()`
- **Symptom**: Without an artificial `busy_delay = 4` ticks after command
  completion, the BIOS poll loop (write command → wait BUSY=1 → wait
  BUSY=0) never sees BUSY rise and hangs forever.
- **Root cause**: All FDC commands execute synchronously in a single call
  to `fdc_write()`.  The real WD1793 takes milliseconds: Type I commands
  3–30 ms depending on step rate (r1r0 bits), Type II commands ~200 ms
  per disk revolution at 300 RPM.
- **Proper fix**: Implement a state machine in `fdc_tick()`.  Commands
  transition the FDC into states like `SEEKING`, `READING`, `WRITING`;
  the tick function counts down real timing delays and advances the FSM.
  BUSY is held naturally for the duration of the command.  Step rate
  should be derived from the command's r1r0 bits (6/12/20/30 ms at
  7.5 MHz = 45000/90000/150000/225000 ticks).
