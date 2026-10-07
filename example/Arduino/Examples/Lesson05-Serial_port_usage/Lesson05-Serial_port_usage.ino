#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "esp_heap_caps.h"

static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
static constexpr int WIFI_UART_TX_PIN = 43;
static constexpr int WIFI_UART_RX_PIN = 44;
static constexpr int POWER_RAIL_1 = 1;
static constexpr int POWER_RAIL_2 = 2;
static constexpr int POWER_LIGHT_GPIO = 40;
static constexpr int BACKLIGHT_GPIO = 46;
static constexpr int DRAW_BUFFER_LINES = 40;
static constexpr size_t AT_RESPONSE_SIZE = 512;

// Replace these values with the Wi-Fi network used in the classroom.
static const char WIFI_SSID[] = "yanfa1";
static const char WIFI_PASSWORD[] = "1223334444yanfa";

HardwareSerial wifiSerial(1);

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
lv_obj_t *titleLabel = nullptr;
lv_obj_t *statusLabel = nullptr;
lv_obj_t *detailLabel = nullptr;
uint8_t *buffer1 = nullptr;
uint8_t *buffer2 = nullptr;

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  gfx.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t *>(pixels));
  lv_display_flush_ready(display);
}

void updateStatus(const char *status, const char *detail, uint32_t color) {
  lv_label_set_text(statusLabel, status);
  lv_label_set_text(detailLabel, detail);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(color), 0);
  lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, -5);
  lv_obj_align(detailLabel, LV_ALIGN_CENTER, 0, 34);
  lv_timer_handler();
}

void clearWifiUart() {
  while (wifiSerial.available()) {
    wifiSerial.read();
  }
}

size_t readAtResponse(char *response, size_t capacity, uint32_t timeoutMs) {
  size_t length = 0;
  const uint32_t startTime = millis();

  while (millis() - startTime < timeoutMs && length < capacity - 1) {
    while (wifiSerial.available() && length < capacity - 1) {
      response[length++] = static_cast<char>(wifiSerial.read());
    }
    lv_timer_handler();
    delay(5);
  }

  response[length] = '\0';
  return length;
}

bool sendAtCommand(const char *command, uint32_t timeoutMs,
                   char *response, size_t responseCapacity) {
  clearWifiUart();
  Serial.printf("[WIFI] >> %s\n", command);
  wifiSerial.print(command);
  wifiSerial.print("\r\n");
  wifiSerial.flush();

  readAtResponse(response, responseCapacity, timeoutMs);
  Serial.printf("[WIFI] << %s\n", response);
  return strstr(response, "OK") != nullptr;
}

String extractIpAddress(const char *response) {
  const char *start = strstr(response, "STAIP,\"");
  if (start == nullptr) return "IP assigned";
  start += strlen("STAIP,\"");
  const char *end = strchr(start, '"');
  if (end == nullptr || end <= start) return "IP assigned";
  return String(start).substring(0, end - start);
}

void initDisplay() {
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

  titleLabel = lv_label_create(lv_screen_active());
  lv_label_set_text(titleLabel, "UART Wi-Fi");
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 42);

  statusLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_20, 0);
  detailLabel = lv_label_create(lv_screen_active());
  lv_obj_set_width(detailLabel, 210);
  lv_obj_set_style_text_align(detailLabel, LV_TEXT_ALIGN_CENTER, 0);
  updateStatus("Starting...", "UART1: TX43 / RX44", 0xF5B942);
}

void connectWifiModule() {
  char response[AT_RESPONSE_SIZE];

  updateStatus("Checking module", "Sending AT command", 0xF5B942);
  if (!sendAtCommand("AT", 1000, response, sizeof(response))) {
    updateStatus("Module not found", "Check TX43 / RX44", 0xE74C3C);
    return;
  }

  updateStatus("Module ready", "Configuring station mode", 0x3498DB);
  sendAtCommand("AT+CWMODE=1", 1200, response, sizeof(response));

  char joinCommand[160];
  snprintf(joinCommand, sizeof(joinCommand), "AT+CWJAP=\"%s\",\"%s\"",
           WIFI_SSID, WIFI_PASSWORD);
  updateStatus("Connecting...", WIFI_SSID, 0xF5B942);
  if (!sendAtCommand(joinCommand, 15000, response, sizeof(response))) {
    updateStatus("Connect failed", "Check SSID/password", 0xE74C3C);
    return;
  }

  updateStatus("Wi-Fi connected", "Reading IP address", 0x20C878);
  if (sendAtCommand("AT+CIFSR", 1500, response, sizeof(response))) {
    const String ipAddress = extractIpAddress(response);
    updateStatus("Wi-Fi connected", ipAddress.c_str(), 0x20C878);
  } else {
    updateStatus("Wi-Fi connected", "IP query failed", 0x20C878);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[WIFI] setup start");

  pinMode(POWER_LIGHT_GPIO, OUTPUT);
  digitalWrite(POWER_LIGHT_GPIO, LOW);
  pinMode(POWER_RAIL_1, OUTPUT);
  pinMode(POWER_RAIL_2, OUTPUT);
  digitalWrite(POWER_RAIL_1, HIGH);
  digitalWrite(POWER_RAIL_2, HIGH);
  ledcAttach(BACKLIGHT_GPIO, 5000, 8);
  ledcWrite(BACKLIGHT_GPIO, 128);

  initDisplay();
  wifiSerial.begin(115200, SERIAL_8N1, WIFI_UART_RX_PIN, WIFI_UART_TX_PIN);
  Serial.println("[WIFI] UART1 ready: TX=43 RX=44 baud=115200");
  connectWifiModule();
  Serial.println("[WIFI] setup complete");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
