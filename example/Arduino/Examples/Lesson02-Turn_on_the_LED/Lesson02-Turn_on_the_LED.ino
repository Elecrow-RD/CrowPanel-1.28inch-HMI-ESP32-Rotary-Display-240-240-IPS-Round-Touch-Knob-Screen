#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "esp_heap_caps.h"
#include "CST816D.h"

static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
static constexpr int LED_GPIO = 4;
static constexpr int POWER_RAIL_1 = 1;
static constexpr int POWER_RAIL_2 = 2;
static constexpr int BACKLIGHT_GPIO = 46;
static constexpr int POWER_LIGHT_GPIO = 40;
static constexpr int DRAW_BUFFER_LINES = 40;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel;
  lgfx::Bus_SPI bus;

 public:
  LGFX() {
    auto busConfig = bus.config();
    busConfig.spi_host = SPI2_HOST;
    busConfig.freq_write = 80000000;
    busConfig.freq_read = 20000000;
    busConfig.spi_3wire = true;
    busConfig.pin_sclk = 10;
    busConfig.pin_mosi = 11;
    busConfig.pin_miso = -1;
    busConfig.pin_dc = 3;
    bus.config(busConfig);
    panel.setBus(&bus);

    auto panelConfig = panel.config();
    panelConfig.pin_cs = 9;
    panelConfig.pin_rst = 14;
    panelConfig.memory_width = SCREEN_WIDTH;
    panelConfig.memory_height = SCREEN_HEIGHT;
    panelConfig.panel_width = SCREEN_WIDTH;
    panelConfig.panel_height = SCREEN_HEIGHT;
    panelConfig.offset_x = 0;
    panelConfig.offset_y = 0;
    panelConfig.offset_rotation = 0;
    panelConfig.dummy_read_pixel = 8;
    panelConfig.dummy_read_bits = 1;
    panelConfig.readable = false;
    panelConfig.invert = true;
    panelConfig.rgb_order = false;
    panelConfig.dlen_16bit = false;
    panelConfig.bus_shared = false;
    panel.config(panelConfig);
    setPanel(&panel);
  }
};

LGFX gfx;
CST816D touch(6, 7, 13, 5);
lv_display_t *display = nullptr;
lv_obj_t *label = nullptr;
uint8_t *buffer1 = nullptr;
uint8_t *buffer2 = nullptr;
bool ledOn = false;

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  gfx.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t *>(pixels));
  lv_display_flush_ready(display);
}

void touchRead(lv_indev_t *, lv_indev_data_t *data) {
  uint16_t x = 0;
  uint16_t y = 0;
  uint8_t gesture = 0;
  const bool pressed = touch.getTouch(&x, &y, &gesture);
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = x;
  data->point.y = y;
}

void buttonClicked(lv_event_t *) {
  ledOn = !ledOn;
  digitalWrite(LED_GPIO, ledOn ? HIGH : LOW);
  lv_label_set_text(label, ledOn ? "LED ON" : "LED OFF");
  lv_obj_set_style_text_color(label, lv_color_hex(0xE74C3C), 0);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[GPIO4] setup start");

  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);
  pinMode(POWER_LIGHT_GPIO, OUTPUT);
  digitalWrite(POWER_LIGHT_GPIO, LOW);

  // Enable the board rails used by the LCD and touch power circuit.
  pinMode(POWER_RAIL_1, OUTPUT);
  pinMode(POWER_RAIL_2, OUTPUT);
  digitalWrite(POWER_RAIL_1, HIGH);
  digitalWrite(POWER_RAIL_2, HIGH);

  // ESP32 Arduino Core 3.x LEDC API: attach the LCD backlight once.
  ledcAttach(BACKLIGHT_GPIO, 5000, 8);
  ledcWrite(BACKLIGHT_GPIO, 128);

  Serial.println("[GPIO4] touch init");
  touch.begin();
  Serial.println("[GPIO4] display init");
  if (!gfx.init()) {
    Serial.println("[GPIO4] ERROR: display init failed");
  } else {
    Serial.println("[GPIO4] display init ok");
  }
  gfx.fillScreen(TFT_BLACK);

  // Draw a visible boot marker before LVGL starts. This separates a panel
  // wiring problem from an LVGL buffer or UI problem.
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx.setTextSize(2);
  gfx.drawString("GPIO4 boot", 45, 110);

  Serial.println("[GPIO4] LVGL init");
  lv_init();
  lv_tick_set_cb(millis);

  const size_t bufferSize = SCREEN_WIDTH * DRAW_BUFFER_LINES * sizeof(uint16_t);
  const uint32_t caps = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
  buffer1 = reinterpret_cast<uint8_t *>(heap_caps_malloc(bufferSize, caps));
  buffer2 = reinterpret_cast<uint8_t *>(heap_caps_malloc(bufferSize, caps));
  if (buffer1 == nullptr || buffer2 == nullptr) {
    Serial.println("[GPIO4] PSRAM/internal buffer allocation failed");
    if (buffer1 == nullptr) {
      buffer1 = reinterpret_cast<uint8_t *>(malloc(bufferSize));
    }
    if (buffer2 == nullptr) {
      buffer2 = reinterpret_cast<uint8_t *>(malloc(bufferSize));
    }
  }
  if (buffer1 == nullptr || buffer2 == nullptr) {
    Serial.println("[GPIO4] ERROR: LVGL buffers unavailable");
    return;
  }
  Serial.printf("[GPIO4] buffers ready, PSRAM=%s\n", psramFound() ? "yes" : "no");

  display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, displayFlush);
  lv_display_set_buffers(display, buffer1, buffer2, bufferSize,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t *input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, touchRead);
  lv_indev_set_display(input, display);

  lv_obj_t *button = lv_button_create(lv_screen_active());
  lv_obj_set_size(button, 150, 70);
  lv_obj_center(button);
  lv_obj_add_event_cb(button, buttonClicked, LV_EVENT_CLICKED, nullptr);
  label = lv_label_create(button);
  lv_label_set_text(label, "LED OFF");
  lv_obj_set_style_text_color(label, lv_color_hex(0xE74C3C), 0);
  lv_obj_center(label);
  Serial.println("[GPIO4] setup complete");
}

void loop() {
  static uint32_t lastHeartbeat = 0;

  lv_timer_handler();
  if (millis() - lastHeartbeat >= 2000) {
    lastHeartbeat = millis();
    Serial.printf("[GPIO4] running, LED=%s\n", ledOn ? "ON" : "OFF");
  }
  delay(5);
}
