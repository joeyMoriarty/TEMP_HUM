/*
 * OLED SPI Hardware Test — SH1106 128x64
 *
 * Wiring:
 *   OLED D0 (CLK)  → D18 (GPIO18)
 *   OLED D1 (MOSI) → D23 (GPIO23)
 *   OLED RES (RST) → D15 (GPIO15)
 *   OLED DC        → D4  (GPIO4)
 *   OLED CS        → D5  (GPIO5)
 *   OLED VCC       → 3V3
 *   OLED GND       → GND
 *
 * Watch Serial Monitor at 115200 baud
 */

#include <U8g2lib.h>
#include <SPI.h>

#define OLED_CS   5
#define OLED_DC   4
#define OLED_RST  15
#define OLED_MOSI 23
#define OLED_CLK  18

U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI oled(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== OLED SPI Test ===");

  bool ok = oled.begin();
  Serial.printf("oled.begin(): %s\n", ok ? "OK" : "FAILED — check wiring");
  if (!ok) return;

  // Test 1: fill screen
  Serial.println("Test 1: fill screen...");
  oled.clearBuffer();
  oled.setDrawColor(1);
  oled.drawBox(0, 0, 128, 64);
  oled.sendBuffer();
  delay(1000);

  // Test 2: text
  Serial.println("Test 2: text...");
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(10, 20, "OLED Working!");
  oled.drawStr(10, 36, "SH1106 128x64");
  oled.drawStr(22, 52, "SPI Mode OK");
  oled.sendBuffer();
  delay(2000);

  // Test 3: shapes
  Serial.println("Test 3: shapes...");
  oled.clearBuffer();
  oled.drawFrame(0, 0, 128, 64);
  oled.drawLine(0, 0, 127, 63);
  oled.drawLine(127, 0, 0, 63);
  oled.drawCircle(64, 32, 24);
  oled.sendBuffer();

  Serial.println("\nAll tests done — check the screen.");
}

void loop() {}
