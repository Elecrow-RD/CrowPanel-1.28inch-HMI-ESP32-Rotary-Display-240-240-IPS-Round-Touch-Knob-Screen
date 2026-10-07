#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Crowbits_DHT20.h>
#include "esp_heap_caps.h"

/*---------------------------------------------------------------
 * Sensor and display configuration
 * The DHT20 uses the secondary I2C bus while LVGL renders to the LCD.
 *--------------------------------------------------------------*/

static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
static constexpr int DHT20_SDA_PIN = 38;
static constexpr int DHT20_SCL_PIN = 39;
static constexpr int POWER_RAIL_1 = 1;
static constexpr int POWER_RAIL_2 = 2;
static constexpr int POWER_LIGHT_GPIO = 40;
static constexpr int BACKLIGHT_GPIO = 46;
static constexpr int DRAW_BUFFER_LINES = 40;
static constexpr uint32_t SENSOR_INTERVAL_MS = 2000;

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel;
  lgfx::Bus_SPI bus;

 public:
  LGFX() {
    auto busConfig = bus.config();
    busConfig.spi_host = SPI2_HOST;
    busConfig.freq_write = 80000000;
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
    panel.config(panelConfig);
    setPanel(&panel);
  }
};

LGFX gfx;
Crowbits_DHT20 sensor(&Wire1);
lv_obj_t *sensorLabel = nullptr;
uint8_t *buffer1 = nullptr;
uint8_t *buffer2 = nullptr;

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  gfx.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t *>(pixels));
  lv_display_flush_ready(display);
}

/**
 * @brief Trigger a DHT20 measurement and convert its raw response.
 * @param temperature Output temperature in degrees Celsius.
 * @param humidity Output relative humidity in percent.
 * @return true when a complete and ready response is received; false otherwise.
 * @note Called by loop() every SENSOR_INTERVAL_MS milliseconds.
 */
bool readDht20(float &temperature, float &humidity) {
  const uint8_t command[] = {0xAC, 0x33, 0x00};
  Wire1.beginTransmission(0x38);
  Wire1.write(command, sizeof(command));
  if (Wire1.endTransmission() != 0) return false;
  delay(80);
  if (Wire1.requestFrom(0x38, 6) != 6) return false;

  uint8_t data[6];
  for (uint8_t i = 0; i < sizeof(data); ++i) data[i] = Wire1.read();
  if ((data[0] & 0x80) != 0) return false;

  const uint32_t rawHumidity = (static_cast<uint32_t>(data[1]) << 12) |
                               (static_cast<uint32_t>(data[2]) << 4) |
                               (data[3] >> 4);
  const uint32_t rawTemperature =
      (static_cast<uint32_t>(data[3] & 0x0F) << 16) |
      (static_cast<uint32_t>(data[4]) << 8) | data[5];

  humidity = (rawHumidity * 100.0f) / 1048576.0f;
  temperature = (rawTemperature * 200.0f) / 1048576.0f - 50.0f;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[DHT20] setup start");
  pinMode(POWER_LIGHT_GPIO, OUTPUT);
  digitalWrite(POWER_LIGHT_GPIO, LOW);
  pinMode(POWER_RAIL_1, OUTPUT);
  pinMode(POWER_RAIL_2, OUTPUT);
  digitalWrite(POWER_RAIL_1, HIGH);
  digitalWrite(POWER_RAIL_2, HIGH);
  ledcAttach(BACKLIGHT_GPIO, 5000, 8);
  ledcWrite(BACKLIGHT_GPIO, 128);
  Wire1.begin(DHT20_SDA_PIN, DHT20_SCL_PIN);
  const int sensorStatus = sensor.begin();
  Serial.printf("[DHT20] sensor begin: %s\n", sensorStatus == 0 ? "ok" : "failed");
  gfx.init();
  gfx.fillScreen(TFT_BLACK);

  lv_init();
  lv_tick_set_cb(millis);
  const size_t bufferSize = SCREEN_WIDTH * DRAW_BUFFER_LINES * sizeof(uint16_t);
  const uint32_t caps = psramFound() ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
  buffer1 = reinterpret_cast<uint8_t *>(heap_caps_malloc(bufferSize, caps));
  buffer2 = reinterpret_cast<uint8_t *>(heap_caps_malloc(bufferSize, caps));
  if (buffer1 == nullptr) buffer1 = reinterpret_cast<uint8_t *>(malloc(bufferSize));
  if (buffer2 == nullptr) buffer2 = reinterpret_cast<uint8_t *>(malloc(bufferSize));

  lv_display_t *display = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, displayFlush);
  lv_display_set_buffers(display, buffer1, buffer2, bufferSize,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  sensorLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(sensorLabel, &lv_font_montserrat_20, 0);
  lv_label_set_text(sensorLabel, "Reading DHT20...");
  lv_obj_center(sensorLabel);
  Serial.println("[DHT20] setup complete");
}

void loop() {
  static uint32_t lastReadTime = 0 - SENSOR_INTERVAL_MS;

  if (millis() - lastReadTime >= SENSOR_INTERVAL_MS) {
    lastReadTime = millis();
    float temperature = 0.0f;
    float humidity = 0.0f;
    char text[64];

    if (!readDht20(temperature, humidity)) {
      snprintf(text, sizeof(text), "DHT20 read error");
    } else {
      snprintf(text, sizeof(text), "Temperature: %.1f C\nHumidity: %.1f %%",
               temperature, humidity);
    }
    lv_label_set_text(sensorLabel, text);
    Serial.println(text);
  }

  lv_timer_handler();
  delay(5);
}
