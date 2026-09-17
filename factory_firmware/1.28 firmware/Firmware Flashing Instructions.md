# RotaryScreen 1.28 Touch-Swipe Version Firmware Flashing Instructions

## Firmware Information

- Target chip: ESP32-S3
- Flash capacity: 16 MB
- Flash mode: DIO, 80 MHz
- PSRAM: OPI
- Compilation core: ESP32 Arduino 3.3.8-cn
- Build date: 2026-09-15
- Application usage: 2,098,515 bytes (approx. 20%)
- Dynamic memory usage: 26,592 bytes (approx. 8%)

This version adds touch-swipe selection on the sound, temperature, and brightness selection pages:

- Swipe left: select the next item
- Swipe right: select the previous item
- The rotary knob and touch input share the same selection state
- Each swipe switches only one item

## Recommended Method: Flash the Merged Firmware

Merged firmware file:

`firmware/RotaryScreen_1_28.ino.merged.bin`

Flashing start address:

`0x0`

### Windows One-Click Script

1. Connect the rotary screen using a USB cable that supports data transfer.
2. Check the COM port assigned to the device in Windows Device Manager, for example `COM5`.
3. Double-click `烧录合并固件.bat`.
4. Enter the COM port and press Enter.
5. Wait for the erase and write operations to complete. After `Hard resetting via RTS pin` or a success message appears, the device will restart automatically.

If the device cannot enter download mode automatically:

1. Press and hold the BOOT button on the device.
2. Briefly press the RESET button once.
3. Release the BOOT button.
4. Run the flashing script again.

If the 921600 baud rate is unstable, you can edit the script and change `921600` to `460800` or `115200`.

## Espressif Flash Download Tool

When using the Espressif Flash Download Tool:

1. Select `ESP32-S3` for ChipType.
2. Select `Develop` for WorkMode.
3. Select `UART` for LoadMode.
4. Add `RotaryScreen_1_28.ino.merged.bin`.
5. Enter `0x0` as the address.
6. Select `80MHz` for SPI SPEED and `DIO` for SPI MODE.
7. Select `16MB` for FLASH SIZE.
8. Select the correct COM port and baud rate, then click START.

It is recommended to erase the Flash once before flashing the merged firmware, to prevent old partition data from affecting startup.

## Partition File Flashing Method

If the flashing tool does not support the merged firmware, use the following addresses:

| Address | File |
| --- | --- |
| `0x0000` | `RotaryScreen_1_28.ino.bootloader.bin` |
| `0x8000` | `RotaryScreen_1_28.ino.partitions.bin` |
| `0xE000` | `boot_app0.bin` |
| `0x10000` | `RotaryScreen_1_28.ino.bin` |

You can also run `烧录分区固件.bat` directly.

## Arduino IDE Recompilation Parameters

- Board: ESP32S3 Dev Module
- CPU Frequency: 240 MHz
- Flash Mode: QIO (the exported flashing parameters use DIO)
- Flash Size: 16 MB
- Partition Scheme: elecrow_s3
- PSRAM: OPI PSRAM
- Upload Speed: 921600
- USB Mode: Hardware CDC and JTAG
- Arduino Runs On: Core 1
- Events Run On: Core 1

The project dependency libraries are located in the `libraries` directory of the complete project, including LVGL, LovyanGFX, Adafruit NeoPixel, and UI resources.

## Post-Flashing Checks

1. The screen starts up normally and displays the selection interface.
2. Rotating the knob can select among sound, temperature, and brightness.
3. Swiping left moves to the next item.
4. Swiping right moves to the previous item.
5. After entering the three function pages, the knob can still adjust values normally.

If the swipe direction is opposite to the enclosure installation orientation, you need to swap the actions corresponding to `SlideLeft` and `SlideRight` in the source code, then recompile.
