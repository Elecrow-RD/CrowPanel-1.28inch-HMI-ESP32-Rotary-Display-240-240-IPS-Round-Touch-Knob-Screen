#include "CST816D.h"

/*---------------------------------------------------------------
 * Driver construction
 * Preserve the board wiring for use when begin() configures the controller.
 *--------------------------------------------------------------*/

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
CST816D::CST816D(int8_t sda_pin, int8_t scl_pin, int8_t rst_pin, int8_t int_pin) {
  _sda = sda_pin;
  _scl = scl_pin;
  _rst = rst_pin;
  _int = int_pin;
}

/*---------------------------------------------------------------
 * Controller initialization
 * Start the secondary I2C bus, pulse the optional control pins, and place
 * the controller in its normal operating state.
 *--------------------------------------------------------------*/

/**
 * @brief Initialize the I2C bus and reset the touch controller.
 *
 * @param None.
 * @return Nothing.
 * @note Called once from setup() before LVGL begins reading touch input.
 */
void CST816D::begin(void) {
  if (_sda != -1 && _scl != -1) {
    Wire1.begin(_sda, _scl);
  } else {
    Wire1.begin();
  }

  // The interrupt line requires a short pulse during controller startup.
  if (_int != -1) {
    pinMode(_int, OUTPUT);
    digitalWrite(_int, HIGH);
    delay(1);
    digitalWrite(_int, LOW);
    delay(1);
  }

  // A complete hardware reset guarantees known touch-controller state.
  if (_rst != -1) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(300);
  }

  i2c_write(0xFE, 0xFF);
}

/*---------------------------------------------------------------
 * Touch sampling
 * Read the finger flag, a supported gesture, and both coordinates in one
 * sampling operation for the LVGL input callback.
 *--------------------------------------------------------------*/

/**
 * @brief Read the current touch state, coordinates, and gesture.
 *
 * @param x Output pointer for the horizontal coordinate.
 * @param y Output pointer for the vertical coordinate.
 * @param gesture Output pointer for a supported gesture code.
 * @return true when a finger is detected; false when released.
 * @note Called by the LVGL touch callback for every input sample.
 */
bool CST816D::getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture) {
  bool fingerDetected = (bool)i2c_read(0x02);

  *gesture = i2c_read(0x01);
  if (*gesture != SlideUp && *gesture != SlideDown) {
    *gesture = None;
  }

  uint8_t data[4];
  i2c_read_continuous(0x03, data, 4);
  *x = ((data[0] & 0x0F) << 8) | data[1];
  *y = ((data[2] & 0x0F) << 8) | data[3];

  return fingerDetected;
}

/*---------------------------------------------------------------
 * I2C register helpers
 * These private functions implement the transaction patterns required by
 * the CST816D data sheet and keep them out of the public interface.
 *--------------------------------------------------------------*/

/**
 * @brief Read one controller register.
 *
 * The repeated-start transaction is retried until the controller returns
 * one byte, which avoids reporting an incomplete touch sample.
 *
 * @param addr Register address.
 * @return Value read from the selected register.
 * @note Used internally whenever a single status byte is required.
 */
uint8_t CST816D::i2c_read(uint8_t addr) {
  uint8_t rdData = 0;
  uint8_t rdDataCount;

  do {
    Wire1.beginTransmission(I2C_ADDR_CST816D);
    Wire1.write(addr);
    Wire1.endTransmission(false);
    rdDataCount = Wire1.requestFrom(I2C_ADDR_CST816D, 1);
  } while (rdDataCount == 0);

  while (Wire1.available()) {
    rdData = Wire1.read();
  }

  return rdData;
}

/**
 * @brief Read consecutive controller registers into a buffer.
 *
 * @param addr Address of the first register.
 * @param data Destination buffer.
 * @param length Number of bytes to read.
 * @return 0 on success; 255 when the register-address transaction fails.
 * @note Used internally to read the four coordinate bytes together.
 */
uint8_t CST816D::i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length) {
  Wire1.beginTransmission(I2C_ADDR_CST816D);
  Wire1.write(addr);
  if (Wire1.endTransmission(true)) return UINT8_MAX;

  Wire1.requestFrom(I2C_ADDR_CST816D, length);
  for (uint32_t i = 0; i < length; i++) {
    *data++ = Wire1.read();
  }

  return 0;
}

/**
 * @brief Write one value to a controller register.
 *
 * @param addr Register address.
 * @param data Value to write.
 * @return Nothing.
 * @note Used internally during controller initialization.
 */
void CST816D::i2c_write(uint8_t addr, uint8_t data) {
  Wire1.beginTransmission(I2C_ADDR_CST816D);
  Wire1.write(addr);
  Wire1.write(data);
  Wire1.endTransmission();
}
