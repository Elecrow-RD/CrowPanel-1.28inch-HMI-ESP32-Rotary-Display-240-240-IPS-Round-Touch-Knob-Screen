#ifndef _CST816D_H
#define _CST816D_H

#include <Wire.h>

// Fixed I2C address used by the CST816D touch controller.
#define I2C_ADDR_CST816D 0x15

/*---------------------------------------------------------------
 * Touch gesture identifiers
 * The controller reports these numeric codes together with coordinates.
 * The application currently uses coordinates but keeps the public gesture
 * values available for future interface extensions.
 *--------------------------------------------------------------*/

enum GESTURE {
  None = 0x00,
  SlideDown = 0x01,
  SlideUp = 0x02,
  SlideLeft = 0x03,
  SlideRight = 0x04,
  SingleTap = 0x05,
  DoubleTap = 0x0B,
  LongPress = 0x0C
};

/*---------------------------------------------------------------
 * CST816D touch-controller interface
 * The class owns the controller pins and hides the low-level I2C register
 * transactions from the main application.
 *--------------------------------------------------------------*/

class CST816D {
 public:
  /**
   * @brief Store the pins used by one CST816D controller.
   *
   * @param sda_pin I2C data pin, or -1 to use the board default.
   * @param scl_pin I2C clock pin, or -1 to use the board default.
   * @param rst_pin Hardware reset pin, or -1 if not connected.
   * @param int_pin Interrupt pin, or -1 if not connected.
   * @return A configured CST816D driver object.
   * @note Called when the global touch object is constructed.
   */
  CST816D(int8_t sda_pin = -1, int8_t scl_pin = -1, int8_t rst_pin = -1, int8_t int_pin = -1);

  /**
   * @brief Initialize the I2C bus and reset the touch controller.
   *
   * @param None.
   * @return Nothing.
   * @note Called once from setup() before LVGL begins reading touch input.
   */
  void begin(void);

  /**
   * @brief Read the current touch state, coordinates, and gesture.
   *
   * @param x Output pointer for the horizontal coordinate.
   * @param y Output pointer for the vertical coordinate.
   * @param gesture Output pointer for a supported gesture code.
   * @return true when a finger is detected; false when released.
   * @note Called by the LVGL touch callback for every input sample.
   */
  bool getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture);

 private:
  // Physical pins assigned when the driver object is constructed.
  int8_t _sda;
  int8_t _scl;
  int8_t _rst;
  int8_t _int;

  uint8_t i2c_read(uint8_t addr);
  uint8_t i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length);
  void i2c_write(uint8_t addr, uint8_t data);
};

#endif
