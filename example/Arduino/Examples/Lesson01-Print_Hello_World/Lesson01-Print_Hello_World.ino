/*---------------------------------------------------------------
 * Basic serial output
 * Demonstrate one-time initialization followed by periodic messages.
 *--------------------------------------------------------------*/

/**
 * @brief Initialize the serial connection and print the first message.
 *
 * @return Nothing.
 * @note Called once after reset.
 */
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Hello World");
}

/**
 * @brief Print a heartbeat message at a readable interval.
 *
 * @return Nothing.
 * @note Called repeatedly by the Arduino framework.
 */
void loop() {
  delay(1000);
  Serial.println("Hello World");
}
