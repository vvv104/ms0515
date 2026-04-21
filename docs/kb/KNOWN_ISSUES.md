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
