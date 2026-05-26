# Firmware Binaries

Place compiled Inputronic BRIDGE firmware `.bin` files here.

The firmware flasher executable looks for `.bin` files in this folder automatically.

## Compiling the firmware

Compile `extras/firmware/firmware.ino` with Arduino IDE or arduino-cli targeting the
Dasduino ConnectPlus board. Copy the output `.bin` to this folder (`extras/firmware_binaries/`).

For a merged binary (bootloader + partitions + app, flashed at address `0x0`):

```
arduino-cli compile --fqbn Dasduino_Boards:esp32:connectplus \
  --export-binaries extras/firmware/
```

The merged binary will be at `extras/firmware/build/.../firmware.ino.merged.bin`.

## Flash address

Always flashed at `0x0` (merged binary).
