#include <lvgl.h>
#include <LovyanGFX.hpp>
#include "esp_heap_caps.h"

/*---------------------------------------------------------------
 * Board and input configuration
 * Keep these constants aligned with the board wiring and display size.
 *--------------------------------------------------------------*/

static constexpr int SCREEN_WIDTH = 240;
static constexpr int SCREEN_HEIGHT = 240;
static constexpr int ENCODER_A_PIN = 45;
static constexpr int ENCODER_B_PIN = 42;
static constexpr int ENCODER_BUTTON_PIN = 41;
static constexpr int LED_GPIO = 4;
static constexpr int BACKLIGHT_GPIO = 46;
static constexpr int POWER_RAIL_1 = 1;
static constexpr int POWER_RAIL_2 = 2;
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
    panelConfig.invert = true;
    panelConfig.readable = false;
    panelConfig.bus_shared = false;
    panel.config(panelConfig);
    setPanel(&panel);
  }
};

LGFX gfx;
lv_obj_t *brightnessLabel = nullptr;
lv_obj_t *ledLabel = nullptr;
uint8_t *buffer1 = nullptr;
uint8_t *buffer2 = nullptr;

int brightnessPercent = 50;
bool ledOn = false;
uint8_t lastEncoderState = 0;
int8_t encoderQuarterSteps = 0;
bool lastButtonState = HIGH;
uint32_t lastButtonChange = 0;

const int8_t ENCODER_TRANSITIONS[16] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
 -1, 0, 0, 1,
  0, 1, -1, 0
};

void displayFlush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
  const int width = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  gfx.pushImage(area->x1, area->y1, width, height,
                reinterpret_cast<lgfx::rgb565_t *>(pixels));
  lv_display_flush_ready(display);
}

/**
 * @brief Refresh the labels that mirror the hardware state.
 * @return Nothing.
 * @note Called after a brightness or LED change.
 */
void updateScreenLabels() {
  char brightnessText[32];
  snprintf(brightnessText, sizeof(brightnessText), "Brightness: %d%%",
           brightnessPercent);
  lv_label_set_text(brightnessLabel, brightnessText);
  lv_label_set_text(ledLabel, ledOn ? "LED ON" : "LED OFF");
  lv_obj_set_style_text_color(ledLabel,
                              lv_color_hex(ledOn ? 0x20C878 : 0xE74C3C), 0);
}

/**
 * @brief Convert the selected percentage into an 8-bit backlight duty cycle.
 * @return Nothing.
 * @note Called after a complete encoder detent is detected.
 */
void applyBrightness() {
  const uint32_t duty = (brightnessPercent * 255UL) / 100UL;
  ledcWrite(BACKLIGHT_GPIO, duty);
  updateScreenLabels();
  Serial.printf("[ENCODER] brightness=%d%%\n", brightnessPercent);
}

/**
 * @brief Decode quadrature transitions and update brightness.
 * @return Nothing.
 * @note Called continuously from loop().
 */
void processEncoder() {
  const uint8_t currentState =
      (static_cast<uint8_t>(digitalRead(ENCODER_A_PIN)) << 1) |
      static_cast<uint8_t>(digitalRead(ENCODER_B_PIN));
  const uint8_t transition = (lastEncoderState << 2) | currentState;
  lastEncoderState = currentState;

  encoderQuarterSteps += ENCODER_TRANSITIONS[transition];
  if (encoderQuarterSteps >= 4) {
    brightnessPercent = constrain(brightnessPercent + 5, 0, 100);
    encoderQuarterSteps = 0;
    applyBrightness();
  } else if (encoderQuarterSteps <= -4) {
    brightnessPercent = constrain(brightnessPercent - 5, 0, 100);
    encoderQuarterSteps = 0;
    applyBrightness();
  }
}

/**
 * @brief Debounce the encoder push button and toggle the LED.
 * @return Nothing.
 * @note Called continuously from loop().
 */
void processEncoderButton() {
  const bool buttonState = digitalRead(ENCODER_BUTTON_PIN);
  if (buttonState != lastButtonState && millis() - lastButtonChange >= 30) {
    lastButtonChange = millis();
    lastButtonState = buttonState;
    if (buttonState == LOW) {
      ledOn = !ledOn;
      digitalWrite(LED_GPIO, ledOn ? HIGH : LOW);
      updateScreenLabels();
      Serial.printf("[ENCODER] GPIO4=%s\n", ledOn ? "HIGH" : "LOW");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[ENCODER] setup start");

  pinMode(POWER_LIGHT_GPIO, OUTPUT);
  digitalWrite(POWER_LIGHT_GPIO, LOW);
  pinMode(POWER_RAIL_1, OUTPUT);
  pinMode(POWER_RAIL_2, OUTPUT);
  digitalWrite(POWER_RAIL_1, HIGH);
  digitalWrite(POWER_RAIL_2, HIGH);

  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);
  pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_GPIO, OUTPUT);
  digitalWrite(LED_GPIO, LOW);

  ledcAttach(BACKLIGHT_GPIO, 5000, 8);
  ledcWrite(BACKLIGHT_GPIO, (brightnessPercent * 255UL) / 100UL);
  lastEncoderState = (digitalRead(ENCODER_A_PIN) << 1) |
                     digitalRead(ENCODER_B_PIN);
  lastButtonState = digitalRead(ENCODER_BUTTON_PIN);

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

  lv_obj_t *title = lv_label_create(lv_screen_active());
  lv_label_set_text(title, "Encoder Control");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

  brightnessLabel = lv_label_create(lv_screen_active());
  lv_obj_set_style_text_font(brightnessLabel, &lv_font_montserrat_20, 0);
  lv_obj_align(brightnessLabel, LV_ALIGN_CENTER, 0, -10);

  ledLabel = lv_label_create(lv_screen_active());
  lv_obj_align(ledLabel, LV_ALIGN_CENTER, 0, 35);
  updateScreenLabels();
  Serial.println("[ENCODER] setup complete");
}

void loop() {
  processEncoder();
  processEncoderButton();
  lv_timer_handler();
  delay(2);
}
