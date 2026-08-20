#define LGFX_USE_V1

#include <lvgl.h>
#include <LovyanGFX.hpp>
#include <Adafruit_NeoPixel.h>
#include <esp_system.h>

#include "CST816D.h"
#include "ui.h"

/*---------------------------------------------------------------
 * Hardware pin assignment
 * Keep all board-specific connections in one place so that the
 * application logic below does not contain unexplained pin values.
 *--------------------------------------------------------------*/

#define TP_I2C_SDA_PIN 6
#define TP_I2C_SCL_PIN 7
#define TP_INT 5
#define TP_RST 13

#define POWER_LIGHT_PIN 40
#define LED_PIN 48
#define LED_NUM 5

#define ENCODER_A_PIN 45
#define ENCODER_B_PIN 42
#define SWITCH_PIN 41

#define SCREEN_BACKLIGHT_PIN 46

/*---------------------------------------------------------------
 * Display driver configuration
 * LovyanGFX connects the ESP32-S3 SPI bus to the 240 x 240 GC9A01
 * panel used by the rotary screen.
 *--------------------------------------------------------------*/

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel_instance;
  lgfx::Bus_SPI _bus_instance;

 public:
  /**
   * @brief Configure the SPI bus and the GC9A01 display panel.
   *
   * The constructor records the physical wiring and timing required by
   * this board. The configuration is applied before setup() initializes
   * the display hardware.
   *
   * @param None.
   * @return A configured LGFX device instance.
   */
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 20000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 10;
      cfg.pin_mosi = 11;
      cfg.pin_miso = -1;
      cfg.pin_dc = 3;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 9;
      cfg.pin_rst = 14;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 240;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

/*---------------------------------------------------------------
 * Device objects and display buffers
 * These objects connect the application to the display, touch panel,
 * decorative LEDs, and LVGL frame buffers.
 *--------------------------------------------------------------*/

LGFX gfx;
CST816D touch(TP_I2C_SDA_PIN, TP_I2C_SCL_PIN, TP_RST, TP_INT);
Adafruit_NeoPixel led(LED_NUM, LED_PIN, NEO_GRB + NEO_KHZ800);

static const uint32_t screenWidth = 240;
static const uint32_t screenHeight = 240;

// Full-screen buffers are allocated in PSRAM during setup().
static uint8_t *buf = NULL;
static uint8_t *buf1 = NULL;

// Identifies the screen currently managed by the application.
lv_obj_t *current_screen = NULL;

// Stores the selected icon on the main screen: 0 volume, 1 temperature, 2 brightness.
static int screen1_index = 1;

/*---------------------------------------------------------------
 * Encoder event state
 * The interrupt and sampling task never call LVGL directly. They place
 * compact actions in a queue for loop() to process on the LVGL thread.
 *--------------------------------------------------------------*/

volatile int8_t position_tmp = -1;
volatile unsigned long lastPressTime = 0;
volatile int clickCount = 0;

const unsigned long debounceTime = 20;
const unsigned long doubleClickTime = 300;

enum EncoderActionType : int8_t {
  ENCODER_ROTATE_CW = 1,
  ENCODER_ROTATE_CCW = 2,
  ENCODER_CLICK = 3,
  ENCODER_DOUBLE_CLICK = 4
};

struct EncoderAction {
  EncoderActionType type;
};

QueueHandle_t encoderActionQueue = NULL;
int currentStateCLK;
int lastStateCLK;

/*---------------------------------------------------------------
 * Backlight and LED animation state
 * The backlight uses an 8-bit PWM duty cycle. Separate counters support
 * the decorative LED task without sharing state with the interface.
 *--------------------------------------------------------------*/

const int pwmFreq = 5000;
const int pwmResolution = 8;

uint8_t 
 = 0;
int8_t ledBrightness = 0;

/*---------------------------------------------------------------
 * Function declarations
 * The declarations provide a map of the program before the implementation
 * sections introduce each subsystem in detail.
 *--------------------------------------------------------------*/

void volumeArcEventCb(lv_event_t *e);
void tempArcEventCb(lv_event_t *e);
void lightArcEventCb(lv_event_t *e);
void performClickAction();
void performDoubleClickAction();
void encTask(void *pvParameters);
void handleEncoderRotation();
void processEncoder();
void updateScreen(int index);
void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map);
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);
void initBacklight();
void ledTestTask(void *pvParameters);

/*---------------------------------------------------------------
 * Encoder button interrupt
 * Record button edges quickly and defer all click interpretation to the
 * encoder task, keeping interrupt execution short and deterministic.
 *--------------------------------------------------------------*/

/**
 * @brief Record a debounced encoder-button press.
 *
 * A falling edge increments the click counter. encTask() later decides
 * whether the sequence represents a single click or a double click.
 *
 * @param None. The interrupt reads SWITCH_PIN directly.
 * @return Nothing.
 * @note Called automatically whenever the encoder button changes state.
 */
void IRAM_ATTR buttonISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime > debounceTime && !digitalRead(SWITCH_PIN)) {
    lastPressTime = interruptTime;
    clickCount++;
  }

  lastInterruptTime = interruptTime;
}

/*---------------------------------------------------------------
 * System initialization
 * Start the hardware drivers, register LVGL callbacks, create the generated
 * interface, and launch the background input and LED tasks.
 *--------------------------------------------------------------*/

/**
 * @brief Initialize the complete rotary-screen application.
 *
 * The function prepares power rails, display DMA, touch input, LVGL buffers,
 * UI callbacks, backlight PWM, the encoder queue, and background tasks.
 *
 * @param None.
 * @return Nothing.
 * @note Arduino calls this function once after every reset.
 */
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("[BOOT] reset_reason=%d\n", (int)esp_reset_reason());

  pinMode(POWER_LIGHT_PIN, OUTPUT);
  digitalWrite(POWER_LIGHT_PIN, LOW);

  // These two rails must remain enabled while the display is operating.
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  touch.begin();

  gfx.init();
  gfx.initDMA();
  gfx.startWrite();
  gfx.setColor(0, 0, 0);
  gfx.setTextSize(2);
  gfx.fillScreen(TFT_BLACK);

  pinMode(ENCODER_A_PIN, INPUT);
  pinMode(ENCODER_B_PIN, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), buttonISR, CHANGE);

  lv_init();
  lv_tick_set_cb(millis);

  // Full-screen buffers in PSRAM allow LVGL to render without consuming most internal RAM.
  size_t buffer_size = screenWidth * screenHeight * sizeof(uint16_t);
  buf = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
  buf1 = (uint8_t *)heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);

  if (!buf) Serial.println("Failed to allocate the first LVGL buffer.");
  if (!buf1) Serial.println("Failed to allocate the second LVGL buffer.");

  lv_display_t *display = lv_display_create(screenWidth, screenHeight);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(display, my_disp_flush);
  lv_display_set_buffers(display, buf, buf1, buffer_size, LV_DISPLAY_RENDER_MODE_FULL);

  lv_indev_t *touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, my_touchpad_read);
  lv_indev_set_display(touch_indev, display);
  delay(100);

  ui_init();
  screen1_index = 1;

  // Volume and temperature now update only their local UI labels.
  lv_obj_add_event_cb(ui_VolumeArc, volumeArcEventCb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(ui_TempArc, tempArcEventCb, LV_EVENT_VALUE_CHANGED, NULL);

  // Brightness remains connected to the physical display backlight.
  lv_obj_add_event_cb(ui_lightArc, lightArcEventCb, LV_EVENT_VALUE_CHANGED, NULL);

  led.begin();
  led.setBrightness(25);
  led.clear();
  led.show();

  delay(500);
  initBacklight();

  encoderActionQueue = xQueueCreate(16, sizeof(EncoderAction));
  if (!encoderActionQueue) {
    Serial.println("Failed to create the encoder event queue.");
  }

  xTaskCreatePinnedToCore(ledTestTask, "LED Test", 2048, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(encTask, "ENC", 2048, NULL, 1, NULL, 0);
}

/*---------------------------------------------------------------
 * Main application loop
 * Consume queued encoder actions on the same thread that services LVGL,
 * then give LVGL time to process animation, input, and redraw timers.
 *--------------------------------------------------------------*/

/**
 * @brief Process encoder actions and service the graphical interface.
 *
 * Queue-based dispatch prevents the encoder task from touching LVGL objects
 * concurrently, which would otherwise cause intermittent resets.
 *
 * @param None.
 * @return Nothing.
 * @note Arduino calls this function repeatedly after setup() completes.
 */
void loop() {
  EncoderAction action;

  while (encoderActionQueue && xQueueReceive(encoderActionQueue, &action, 0) == pdTRUE) {
    if (action.type == ENCODER_ROTATE_CW) {
      position_tmp = 1;
      handleEncoderRotation();
    } else if (action.type == ENCODER_ROTATE_CCW) {
      position_tmp = 0;
      handleEncoderRotation();
    } else if (action.type == ENCODER_CLICK) {
      performClickAction();
    } else if (action.type == ENCODER_DOUBLE_CLICK) {
      performDoubleClickAction();
    }
  }

  lv_timer_handler();
  delay(5);
}

/*---------------------------------------------------------------
 * Child-screen value callbacks
 * Volume and temperature are local interface demonstrations. Brightness
 * additionally changes the physical backlight duty cycle.
 *--------------------------------------------------------------*/

/**
 * @brief Update the volume percentage shown on the volume screen.
 *
 * No command is transmitted to another product; the arc and label remain a
 * self-contained interface demonstration.
 *
 * @param e LVGL value-change event generated by the volume arc.
 * @return Nothing.
 * @note Called whenever touch or encoder input changes the volume value.
 */
void volumeArcEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t *arc = lv_event_get_target_obj(e);
  int value = lv_arc_get_value(arc);
  char volText[8];

  snprintf(volText, sizeof(volText), value == 100 ? "%d%%" : " %d%%", value);
  lv_label_set_text(ui_VolNum, volText);
}

/**
 * @brief Update the temperature value shown on the temperature screen.
 *
 * The value is presented locally and is not converted into a servo command
 * or sent to another product.
 *
 * @param e LVGL value-change event generated by the temperature arc.
 * @return Nothing.
 * @note Called whenever touch or encoder input changes the temperature value.
 */
void tempArcEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t *arc = lv_event_get_target_obj(e);
  int value = lv_arc_get_value(arc);
  char tempText[12];

  snprintf(tempText, sizeof(tempText), "%d°C", value);
  lv_label_set_text(ui_TempNum, tempText);
}

/**
 * @brief Update the brightness label and display backlight.
 *
 * The selected percentage is mapped to the full 8-bit PWM range so the
 * third child screen continues to control the physical screen brightness.
 *
 * @param e LVGL value-change event generated by the brightness arc.
 * @return Nothing.
 * @note Called whenever touch or encoder input changes the brightness value.
 */
void lightArcEventCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;

  lv_obj_t *arc = lv_event_get_target_obj(e);
  int value = constrain(lv_arc_get_value(arc), 0, 100);
  char lightText[8];

  snprintf(lightText, sizeof(lightText), value == 100 ? "%d%%" : " %d%%", value);
  lv_label_set_text(ui_LightNum, lightText);
  ledcWrite(SCREEN_BACKLIGHT_PIN, (value * 255) / 100);
}

/*---------------------------------------------------------------
 * Screen navigation
 * A single click opens the selected child screen. A double click returns
 * from any child screen to the main selection screen.
 *--------------------------------------------------------------*/

/**
 * @brief Open the child screen selected on the main screen.
 *
 * @param None.
 * @return Nothing.
 * @note Called by loop() after the encoder task reports one confirmed click.
 */
void performClickAction() {
  current_screen = lv_screen_active();
  if (current_screen != ui_Screen1) return;

  if (screen1_index == 0) {
    _ui_screen_change(&ui_Screen2, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen2_screen_init);
  } else if (screen1_index == 1) {
    _ui_screen_change(&ui_Screen3, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen3_screen_init);
  } else if (screen1_index == 2) {
    _ui_screen_change(&ui_Screen4, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen4_screen_init);
  }
}

/**
 * @brief Return from a child screen to the main selection screen.
 *
 * @param None.
 * @return Nothing.
 * @note Called by loop() after the encoder task reports a double click.
 */
void performDoubleClickAction() {
  current_screen = lv_screen_active();

  if (current_screen == ui_Screen2 || current_screen == ui_Screen3 || current_screen == ui_Screen4) {
    _ui_screen_change(&ui_Screen1, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, &ui_Screen1_screen_init);
  }
}

/*---------------------------------------------------------------
 * Encoder sampling task
 * Detect rotation direction and classify button sequences without blocking
 * the main LVGL loop.
 *--------------------------------------------------------------*/

/**
 * @brief Sample the rotary encoder and queue high-level input actions.
 *
 * The task converts quadrature edges into clockwise or counterclockwise
 * events and waits for the double-click window before confirming one click.
 *
 * @param pvParameters Unused FreeRTOS task parameter.
 * @return Nothing. The task runs for the lifetime of the application.
 * @note Started once by setup() and pinned to CPU core 0.
 */
void encTask(void *pvParameters) {
  (void)pvParameters;
  lastStateCLK = digitalRead(ENCODER_A_PIN);

  while (1) {
    currentStateCLK = digitalRead(ENCODER_A_PIN);

    if (currentStateCLK != lastStateCLK && currentStateCLK == HIGH && encoderActionQueue) {
      EncoderAction action;
      action.type = digitalRead(ENCODER_B_PIN) != currentStateCLK
                        ? ENCODER_ROTATE_CW
                        : ENCODER_ROTATE_CCW;
      xQueueSend(encoderActionQueue, &action, 0);
    }
    lastStateCLK = currentStateCLK;

    EncoderAction clickAction;
    bool hasClickAction = false;

    if (clickCount >= 2) {
      clickAction.type = ENCODER_DOUBLE_CLICK;
      clickCount = 0;
      hasClickAction = true;
    } else if (clickCount == 1 && millis() - lastPressTime > doubleClickTime) {
      clickAction.type = ENCODER_CLICK;
      clickCount = 0;
      hasClickAction = true;
    }

    if (hasClickAction && encoderActionQueue) {
      xQueueSend(encoderActionQueue, &clickAction, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

/*---------------------------------------------------------------
 * Encoder action handling
 * Rotation changes the selected child value in steps of five or moves the
 * main-screen selection by one position.
 *--------------------------------------------------------------*/

/**
 * @brief Apply one queued encoder rotation to the active screen.
 *
 * Child-screen values are constrained to their designed ranges. A synthetic
 * LVGL value-change event then updates the matching label and, for brightness,
 * the display backlight.
 *
 * @param None. The direction is read from position_tmp.
 * @return Nothing.
 * @note Called by loop() for every queued clockwise or counterclockwise action.
 */
void handleEncoderRotation() {
  current_screen = lv_screen_active();
  int delta = position_tmp == 1 ? 5 : -5;

  if (current_screen == ui_Screen2 && ui_VolumeArc) {
    int value = constrain(lv_arc_get_value(ui_VolumeArc) + delta, 0, 100);
    lv_arc_set_value(ui_VolumeArc, value);
    lv_obj_send_event(ui_VolumeArc, LV_EVENT_VALUE_CHANGED, NULL);
  } else if (current_screen == ui_Screen3 && ui_TempArc) {
    int value = constrain(lv_arc_get_value(ui_TempArc) + delta, 0, 200);
    lv_arc_set_value(ui_TempArc, value);
    lv_obj_send_event(ui_TempArc, LV_EVENT_VALUE_CHANGED, NULL);
  } else if (current_screen == ui_Screen4 && ui_lightArc) {
    int value = constrain(lv_arc_get_value(ui_lightArc) + delta, 0, 100);
    lv_arc_set_value(ui_lightArc, value);
    lv_obj_send_event(ui_lightArc, LV_EVENT_VALUE_CHANGED, NULL);
  } else if (current_screen == ui_Screen1) {
    processEncoder();
  }

  position_tmp = -1;
}

/**
 * @brief Move the main-screen selection according to encoder direction.
 *
 * The selection remains within the three available icons before updateScreen()
 * refreshes the generated UI objects.
 *
 * @param None. The direction is read from position_tmp.
 * @return Nothing.
 * @note Called by handleEncoderRotation() while the main screen is active.
 */
void processEncoder() {
  current_screen = lv_screen_active();
  if (current_screen != ui_Screen1) return;

  if (position_tmp == 1 && screen1_index < 2) {
    screen1_index++;
  } else if (position_tmp == 0 && screen1_index > 0) {
    screen1_index--;
  }

  updateScreen(screen1_index);
  position_tmp = -1;
}

/**
 * @brief Arrange and highlight the three icons on the main screen.
 *
 * The selected icon is centered and colored blue. Neighboring icons are
 * positioned to its left or right in white, reproducing the rotary carousel.
 *
 * @param index Requested selection index from 0 through 2.
 * @return Nothing.
 * @note Called after main-screen encoder rotation and during UI selection updates.
 */
void updateScreen(int index) {
  index = constrain(index, 0, 2);

  lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);

  switch (index) {
    case 0:
      // Center the selected volume icon.
      lv_obj_remove_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_volumeBlue, 0);
      lv_obj_set_x(ui_volumeTextBlue, 0);
      lv_obj_set_x(ui_volumeWhite, 0);
      lv_obj_set_x(ui_volumeTextWhite, 0);

      // Place temperature to the right as the next option.
      lv_obj_remove_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, 70);
      lv_obj_set_x(ui_tempTextBlue, 70);
      lv_obj_set_x(ui_tempWhite, 70);
      lv_obj_set_x(ui_tempTextWhite, 70);

      lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      break;

    case 1:
      // Place volume to the left of the selected temperature icon.
      lv_obj_remove_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_volumeBlue, -70);
      lv_obj_set_x(ui_volumeTextBlue, -70);
      lv_obj_set_x(ui_volumeWhite, -70);
      lv_obj_set_x(ui_volumeTextWhite, -70);

      lv_obj_add_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, 0);
      lv_obj_set_x(ui_tempTextBlue, 0);
      lv_obj_set_x(ui_tempWhite, 0);
      lv_obj_set_x(ui_tempTextWhite, 0);

      // Place brightness to the right as the next option.
      lv_obj_add_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_lightBlue, 70);
      lv_obj_set_x(ui_lightTextBlue, 70);
      lv_obj_set_x(ui_lightWhite, 70);
      lv_obj_set_x(ui_lightTextWhite, 70);
      break;

    case 2:
      lv_obj_add_flag(ui_volumeWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_volumeTextBlue, LV_OBJ_FLAG_HIDDEN);

      // Place temperature to the left of the selected brightness icon.
      lv_obj_add_flag(ui_tempBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_tempTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_tempWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_tempTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_tempBlue, -70);
      lv_obj_set_x(ui_tempTextBlue, -70);
      lv_obj_set_x(ui_tempWhite, -70);
      lv_obj_set_x(ui_tempTextWhite, -70);

      lv_obj_add_flag(ui_lightWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_lightTextWhite, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_lightBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(ui_lightTextBlue, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_x(ui_lightBlue, 0);
      lv_obj_set_x(ui_lightTextBlue, 0);
      lv_obj_set_x(ui_lightWhite, 0);
      lv_obj_set_x(ui_lightTextWhite, 0);
      break;
  }
}

/*---------------------------------------------------------------
 * LVGL display and touch adapters
 * These callbacks translate LVGL operations into LovyanGFX DMA transfers
 * and CST816D touch samples.
 *--------------------------------------------------------------*/

/**
 * @brief Transfer a completed LVGL drawing area to the display.
 *
 * DMA sends the RGB565 pixel buffer efficiently. LVGL is notified only after
 * the transfer completes so it can safely reuse the buffer.
 *
 * @param display LVGL display requesting the transfer.
 * @param area Rectangle containing the pixels to update.
 * @param px_map Pointer to the RGB565 pixel data.
 * @return Nothing.
 * @note Called by LVGL whenever a rendered area is ready for display.
 */
void my_disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map) {
  if (gfx.getStartCount() > 0) {
    gfx.endWrite();
  }

  gfx.pushImageDMA(area->x1,
                   area->y1,
                   area->x2 - area->x1 + 1,
                   area->y2 - area->y1 + 1,
                   (lgfx::rgb565_t *)px_map);
  gfx.waitDMA();
  lv_display_flush_ready(display);
}

/**
 * @brief Provide the current touch state and coordinates to LVGL.
 *
 * @param indev LVGL input device requesting a new sample.
 * @param data Output structure receiving pressed/released state and position.
 * @return Nothing.
 * @note Called periodically by the LVGL input-device timer.
 */
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  uint8_t gesture;
  uint16_t touchX;
  uint16_t touchY;
  bool touched = touch.getTouch(&touchX, &touchY, &gesture);

  if (!touched) {
    data->state = LV_INDEV_STATE_REL;
  } else {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  }
}

/*---------------------------------------------------------------
 * Screen backlight control
 * Attach PWM once at startup and begin at the same 50 percent value shown
 * by the generated brightness screen.
 *--------------------------------------------------------------*/

/**
 * @brief Initialize PWM control for the display backlight.
 *
 * @param None.
 * @return Nothing.
 * @note Called once by setup() after the display and UI are ready.
 */
void initBacklight() {
  ledcAttach(SCREEN_BACKLIGHT_PIN, pwmFreq, pwmResolution);
  ledcWrite(SCREEN_BACKLIGHT_PIN, (50 * 255) / 100);
}

/*---------------------------------------------------------------
 * Decorative LED task
 * Repeatedly demonstrate flowing, flashing, and breathing patterns on the
 * five LEDs beneath the display without blocking the main interface.
 *--------------------------------------------------------------*/

/**
 * @brief Run the decorative LED animation sequence.
 *
 * @param pvParameters Unused FreeRTOS task parameter.
 * @return Nothing. The task repeats the animation indefinitely.
 * @note Started once by setup() and pinned to CPU core 0.
 */
void ledTestTask(void *pvParameters) {
  (void)pvParameters;

  while (1) {
    led.clear();
    led.show();

    // Move one white pixel across the strip for five complete passes.
    while (ledCount++ < 5) {
      for (int i = 0; i < LED_NUM; i++) {
        led.setPixelColor(i, led.Color(255, 255, 255));
        led.show();
        vTaskDelay(pdMS_TO_TICKS(250));
        led.clear();
        led.show();
      }
    }
    ledCount = 0;

    // Flash all five assigned colors rapidly.
    for (int i = 0; i < 5; i++) {
      led.setPixelColor(0, led.Color(255, 0, 0));
      led.setPixelColor(1, led.Color(0, 255, 0));
      led.setPixelColor(2, led.Color(0, 0, 255));
      led.setPixelColor(3, led.Color(255, 255, 0));
      led.setPixelColor(4, led.Color(130, 0, 255));
      led.show();
      vTaskDelay(pdMS_TO_TICKS(100));
      led.clear();
      led.show();
      vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Move a different color through each physical LED position.
    while (ledCount < 5) {
      for (int i = 0; i < LED_NUM; i++) {
        led.clear();
        switch (i) {
          case 0: led.setPixelColor(i, led.Color(255, 0, 0)); break;
          case 1: led.setPixelColor(i, led.Color(0, 255, 0)); break;
          case 2: led.setPixelColor(i, led.Color(0, 0, 255)); break;
          case 3: led.setPixelColor(i, led.Color(255, 255, 0)); break;
          case 4: led.setPixelColor(i, led.Color(130, 0, 255)); break;
        }
        led.show();
        vTaskDelay(pdMS_TO_TICKS(250));
      }
      ledCount++;
    }
    ledCount = 0;

    // Repeat the same color set with a slower simultaneous flash.
    for (int i = 0; i < 5; i++) {
      led.setPixelColor(0, led.Color(255, 0, 0));
      led.setPixelColor(1, led.Color(0, 255, 0));
      led.setPixelColor(2, led.Color(0, 0, 255));
      led.setPixelColor(3, led.Color(255, 255, 0));
      led.setPixelColor(4, led.Color(130, 0, 255));
      led.show();
      vTaskDelay(pdMS_TO_TICKS(250));
      led.clear();
      led.show();
      vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Fade the full color set in and out to create a breathing effect.
    led.setPixelColor(0, led.Color(255, 0, 0));
    led.setPixelColor(1, led.Color(0, 255, 0));
    led.setPixelColor(2, led.Color(0, 0, 255));
    led.setPixelColor(3, led.Color(255, 255, 0));
    led.setPixelColor(4, led.Color(130, 0, 255));

    while (ledCount++ < 10) {
      for (ledBrightness = 0; ledBrightness <= 25; ledBrightness++) {
        led.setBrightness(ledBrightness);
        led.setPixelColor(0, led.Color(255, 0, 0));
        led.setPixelColor(1, led.Color(0, 255, 0));
        led.setPixelColor(2, led.Color(0, 0, 255));
        led.setPixelColor(3, led.Color(255, 255, 0));
        led.setPixelColor(4, led.Color(130, 0, 255));
        led.show();
        vTaskDelay(pdMS_TO_TICKS(50));
      }

      for (; ledBrightness >= 0; ledBrightness--) {
        led.setBrightness(ledBrightness);
        led.show();
        vTaskDelay(pdMS_TO_TICKS(50));
      }

      ledCount++;
    }

    ledCount = 0;
    ledBrightness = 0;
    led.setBrightness(25);
    led.clear();
    led.show();

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
