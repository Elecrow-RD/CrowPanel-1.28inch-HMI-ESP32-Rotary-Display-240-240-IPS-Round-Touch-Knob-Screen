# ESP32-S3 1.28-inch course examples

Lesson order:

1. `Lesson01-Print_Hello_World`
2. `Lesson02-Turn_on_the_LED`
3. `Lesson03-rotate`
4. `Lesson04-Temperature_and_Humidity`
5. `Lesson05-Serial_port_usage`
6. `Lesson06-USB2.0`

These sketches target ESP32 Arduino Core 3.3.8, the bundled LVGL 9.1.0, and the board wiring in `Eagle_SCH&PCB/ESP32 Display-1.28-V1.0.sch`.

## Arduino IDE setup

- Select the ESP32-S3 board definition used by the project and ESP32 Core 3.3.8.
- Enable PSRAM for LVGL examples.
- Select the custom partition table when compiling the full UI project. These six examples use small programs and do not require the large UI assets.
- Copy the `Arduino/libraries` directory into the Arduino libraries location, or use it as the sketchbook library directory. Do not edit third-party library sources.

## Wiring used by the examples

| Function | GPIO |
|---|---:|
| GC9A01 SCLK/MOSI/DC/CS/RST | 10 / 11 / 3 / 9 / 14 |
| CST816D SDA/SCL/RST/INT | 6 / 7 / 13 / 5 |
| DHT20 SDA/SCL | 38 / 39 |
| GPIO LED example | GPIO4 (active-high external LED) |
| Wi-Fi module UART TX/RX | ESP32 GPIO43 / GPIO44, 115200 8N1 |
| Native USB D-/D+ | GPIO19 / GPIO20 |

The UART Wi-Fi sketch controls an ESP-AT-compatible external module. Connect the ESP32 TX GPIO43 to the module RX, and ESP32 RX GPIO44 to the module TX.

## Lesson03-rotate

`Lesson03-rotate` uses encoder A/B on GPIO45/GPIO42 to adjust the LCD backlight, and the encoder push button on GPIO41 to toggle GPIO4. The screen shows the current brightness and LED state.

## Lesson06-USB2.0

Use the ESP32-S3 native USB connector and select **USB-OTG (TinyUSB)** in the Arduino board menu. This must compile with `ARDUINO_USB_MODE=0`; `Hardware CDC and JTAG` (`ARDUINO_USB_MODE=1`) can compile the sketch but cannot enumerate the mouse HID interface. The sketch uses only the Core 3.3.8 `USBHIDMouse` implementation; no external HID library is included.