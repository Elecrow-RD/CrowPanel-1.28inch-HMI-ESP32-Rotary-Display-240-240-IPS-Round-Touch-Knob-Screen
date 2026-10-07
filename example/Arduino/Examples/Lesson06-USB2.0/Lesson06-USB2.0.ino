#include "USB.h"
#include "USBHIDMouse.h"
#include "CST816D.h"
#include <esp_event.h>

#if !defined(ARDUINO_USB_MODE) || ARDUINO_USB_MODE == 1
// Hardware CDC/JTAG mode can compile this sketch, but it cannot expose the
// USB HID mouse interface. Select USB-OTG/TinyUSB mode for actual mouse use.
#define USB_MOUSE_MODE_UNAVAILABLE 1
#else
#define USB_MOUSE_MODE_UNAVAILABLE 0
#endif

USBHIDMouse mouse;
CST816D touch(6, 7, 13, 5);
static constexpr int POWER_RAIL_1 = 1;
static constexpr int POWER_RAIL_2 = 2;
static constexpr int POWER_LIGHT_GPIO = 40;
static constexpr int BACKLIGHT_GPIO = 46;
uint16_t lastX = 0;
uint16_t lastY = 0;
bool trackingTouch = false;
volatile bool usbStarted = false;

void usbEventCallback(void *, esp_event_base_t eventBase,
                      int32_t eventId, void *) {
  if (eventBase != ARDUINO_USB_EVENTS) return;
  if (eventId == ARDUINO_USB_STARTED_EVENT) {
    usbStarted = true;
    Serial.println("[USB MOUSE] USB started");
  } else if (eventId == ARDUINO_USB_STOPPED_EVENT) {
    usbStarted = false;
    Serial.println("[USB MOUSE] USB stopped");
  } else if (eventId == ARDUINO_USB_SUSPEND_EVENT) {
    Serial.println("[USB MOUSE] USB suspended");
  } else if (eventId == ARDUINO_USB_RESUME_EVENT) {
    Serial.println("[USB MOUSE] USB resumed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[USB MOUSE] setup start");
#if USB_MOUSE_MODE_UNAVAILABLE
  Serial.println("[USB MOUSE] WARNING: USBMode=hwcdc");
  Serial.println("[USB MOUSE] Select USB-OTG/TinyUSB (ARDUINO_USB_MODE=0)");
#endif
  pinMode(POWER_LIGHT_GPIO, OUTPUT);
  digitalWrite(POWER_LIGHT_GPIO, LOW);
  pinMode(POWER_RAIL_1, OUTPUT);
  pinMode(POWER_RAIL_2, OUTPUT);
  digitalWrite(POWER_RAIL_1, HIGH);
  digitalWrite(POWER_RAIL_2, HIGH);
  ledcAttach(BACKLIGHT_GPIO, 5000, 8);
  ledcWrite(BACKLIGHT_GPIO, 128);

  USB.onEvent(usbEventCallback);
  touch.begin();
  mouse.begin();
  USB.begin();
  delay(1500);
  Serial.printf("[USB MOUSE] USB state: %s\n", usbStarted ? "started" : "not started");

  // Move in a small square once. If the PC cursor moves, USB HID is working
  // and any later problem is limited to the touch input path.
  if (usbStarted) {
    mouse.move(20, 0, 0);
    delay(100);
    mouse.move(0, 20, 0);
    delay(100);
    mouse.move(-20, 0, 0);
    delay(100);
    mouse.move(0, -20, 0);
  }
  Serial.println("[USB MOUSE] ready; slide on the touch panel");
}

void loop() {
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t gesture = 0;
  const bool touched = touch.getTouch(&x, &y, &gesture);

  if (touched) {
    static uint32_t lastTouchLog = 0;
    if (millis() - lastTouchLog >= 100) {
      lastTouchLog = millis();
      Serial.printf("[TOUCH] x=%u y=%u gesture=%u\n", x, y, gesture);
    }
    if (!trackingTouch) {
      lastX = x;
      lastY = y;
      trackingTouch = true;
    }

    const int16_t rawDeltaX = static_cast<int16_t>(x) - lastX;
    const int16_t rawDeltaY = static_cast<int16_t>(y) - lastY;
    const int8_t deltaX = static_cast<int8_t>(constrain(rawDeltaX, -127, 127));
    const int8_t deltaY = static_cast<int8_t>(constrain(rawDeltaY, -127, 127));
    if (deltaX != 0 || deltaY != 0) {
      mouse.move(deltaX, deltaY, 0);
    }
    if (!mouse.isPressed(MOUSE_LEFT)) {
      mouse.press(MOUSE_LEFT);
    }
    lastX = x;
    lastY = y;
  } else {
    if (mouse.isPressed(MOUSE_LEFT)) {
      mouse.release(MOUSE_LEFT);
    }
    trackingTouch = false;
  }

  delay(8);
}
