/*
 * I2C Scanner — run this to find device addresses
 * Scans both buses:
 *   Bus 1 (AHT10) : SDA=D21 (GPIO21), SCL=D22 (GPIO22)
 *   Bus 2 (OLED)  : SDA=D4  (GPIO4),  SCL=D19 (GPIO19)
 *
 * Open Serial Monitor at 115200 baud after uploading.
 */

#include <Wire.h>

void scanBus(TwoWire& bus, const char* label, int sda, int scl) {
  bus.begin(sda, scl);
  delay(100);
  Serial.printf("\n--- %s (SDA=GPIO%d, SCL=GPIO%d) ---\n", label, sda, scl);

  int found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    bus.beginTransmission(addr);
    if (bus.endTransmission() == 0) {
      Serial.printf("  Device found at 0x%02X", addr);
      if (addr == 0x38) Serial.print("  (AHT10)");
      if (addr == 0x3C) Serial.print("  (OLED — address 0x3C)");
      if (addr == 0x3D) Serial.print("  (OLED — address 0x3D)");
      Serial.println();
      found++;
    }
  }
  if (!found) Serial.println("  No devices found — check wiring!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n====== I2C Scanner ======");

  scanBus(Wire,  "AHT10 bus", 21, 22);
  scanBus(Wire1, "OLED bus",   4, 19);

  Serial.println("\n====== Done ======");
}

void loop() {}
