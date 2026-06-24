/*
 * Room Climate Monitor
 * Board  : ESP32-WROOM
 * Sensor : AHT10  — I2C : D21=SDA, D22=SCL
 * Display: SH1106 — SPI : D18=CLK, D23=MOSI, D15=RST, D4=DC, D5=CS
 *
 * Libraries: U8g2 (oliver), Adafruit AHTX0, Adafruit BusIO
 *
 * Relay control: uncomment blocks in applyRelays() + pinMode block in setup()
 */

#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_AHTX0.h>

// ── Pin map ───────────────────────────────────────────────────────────────────
#define SDA_PIN          21
#define SCL_PIN          22
#define OLED_CLK         18
#define OLED_MOSI        23
#define OLED_RST         15
#define OLED_DC           4
#define OLED_CS           5
#define RELAY_AC         25
#define RELAY_LIGHTS     26
#define RELAY_HUMIDIFIER 27

// ── Layout constants ──────────────────────────────────────────────────────────
// Emoji box occupies the right 40px; left 85px holds T/H/F rows
#define BOX_X  87
#define BOX_Y  14
#define BOX_W  40
#define BOX_H  49
#define EM_CX  107   // BOX_X + BOX_W/2
#define EM_CY   38   // vertical centre of box

// ── Config ────────────────────────────────────────────────────────────────────
const uint32_t READ_INTERVAL_MS = 2000;

struct AutoConfig { float acOnTemp, acOffTemp, humOnRH, humOffRH; };
AutoConfig cfg = { 28.0f, 25.0f, 40.0f, 60.0f };

// ── State ─────────────────────────────────────────────────────────────────────
struct RelayState { bool ac, lights, humidifier; };
struct Reading    { float tempC, rh, feelsLike; bool valid; };

// ── Hardware ──────────────────────────────────────────────────────────────────
U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI oled(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
Adafruit_AHTX0 aht;
RelayState relay   = { false, false, false };
bool sensorPresent = false;

// ── Heat-index (NOAA Steadman, Celsius) ──────────────────────────────────────
float heatIndex(float t, float h) {
  if (t < 27.0f) return t;
  return -8.78469475556f + 1.61139411f*t + 2.33854883889f*h
         - 0.14611605f*t*h  - 0.012308094f*t*t - 0.016424828f*h*h
         + 0.002211732f*t*t*h + 0.00072546f*t*h*h - 0.000003582f*t*t*h*h;
}

// ── Sensor read ───────────────────────────────────────────────────────────────
Reading readSensor() {
  if (!sensorPresent) return { 0, 0, 0, false };
  sensors_event_t hev, tev;
  if (!aht.getEvent(&hev, &tev)) return { 0, 0, 0, false };
  float t = tev.temperature, h = hev.relative_humidity;
  return { t, h, heatIndex(t, h), true };
}

// ── Relay control (uncomment when wired) ─────────────────────────────────────
void applyRelays(const Reading& r) {
  if (!r.valid) return;
  // if (r.tempC > cfg.acOnTemp  && !relay.ac)       { digitalWrite(RELAY_AC, LOW);  relay.ac = true; }
  // if (r.tempC < cfg.acOffTemp &&  relay.ac)        { digitalWrite(RELAY_AC, HIGH); relay.ac = false; }
  // if (r.rh < cfg.humOnRH   && !relay.humidifier)  { digitalWrite(RELAY_HUMIDIFIER, LOW);  relay.humidifier = true; }
  // if (r.rh > cfg.humOffRH  &&  relay.humidifier)  { digitalWrite(RELAY_HUMIDIFIER, HIGH); relay.humidifier = false; }
}

// ── Emoji primitives ──────────────────────────────────────────────────────────
static void emFace()  { oled.drawCircle(EM_CX, EM_CY, 12); }
static void emEyes()  { oled.drawDisc(EM_CX-5, EM_CY-4, 2); oled.drawDisc(EM_CX+5, EM_CY-4, 2); }
static void emSmile() { oled.drawCircle(EM_CX, EM_CY+4,  6, U8G2_DRAW_LOWER_LEFT|U8G2_DRAW_LOWER_RIGHT); }
static void emFrown() { oled.drawCircle(EM_CX, EM_CY+11, 6, U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_UPPER_RIGHT); }
static void emFlat()  { oled.drawHLine(EM_CX-4, EM_CY+5, 9); }
static void emDrop(int x, int y) { oled.drawPixel(x, y); oled.drawDisc(x, y+3, 2); }

// ── Emoji selector ────────────────────────────────────────────────────────────
//
//  FL < 22        → 🥶 cold   : frown + snowflake above
//  FL > 30        → 🥵 hot    : squint eyes + open mouth + heat waves
//  RH > 65        → 💦 sweaty : neutral + 3 drops below face
//  RH < 40        → 🏜 dry    : neutral + crack through face
//  22 ≤ FL ≤ 27   → 😊 happy  : smile
//  27 < FL ≤ 30   → 😐 warm   : neutral + single drop
//
static void drawEmoji(float fl, float rh) {
  oled.drawFrame(BOX_X, BOX_Y, BOX_W, BOX_H);

  if (fl < 22.0f) {
    emFace(); emEyes(); emFrown();
    // snowflake: cross + diagonals centred above face
    int sx = EM_CX, sy = BOX_Y + 6;
    oled.drawHLine(sx-4, sy, 9);
    oled.drawVLine(sx, sy-4, 9);
    oled.drawLine(sx-3, sy-3, sx+3, sy+3);
    oled.drawLine(sx+3, sy-3, sx-3, sy+3);

  } else if (fl > 29.0f) {
    emFace();
    // squinting eyes (horizontal lines)
    oled.drawHLine(EM_CX-8, EM_CY-4, 6);
    oled.drawHLine(EM_CX+2, EM_CY-4, 6);
    // open mouth (filled ellipse outline)
    oled.drawEllipse(EM_CX, EM_CY+5, 4, 3, U8G2_DRAW_ALL);
    // heat waves above face (3 zigzag lines)
    for (int i = 0; i < 3; i++) {
      int wx = EM_CX-6 + i*6, wy = BOX_Y+2;
      oled.drawLine(wx,   wy+3, wx+2, wy);
      oled.drawLine(wx+2, wy,   wx+4, wy+3);
    }

  } else if (rh > 65.0f) {
    emFace(); emEyes(); emFlat();
    // three sweat drops below face
    emDrop(EM_CX-8, EM_CY+13);
    emDrop(EM_CX,   EM_CY+13);
    emDrop(EM_CX+8, EM_CY+13);

  } else if (rh < 40.0f) {
    emFace(); emEyes(); emFlat();
    // zigzag crack through face
    oled.drawLine(EM_CX-1, EM_CY-8, EM_CX+2, EM_CY-2);
    oled.drawLine(EM_CX+2, EM_CY-2, EM_CX-1, EM_CY+4);

  } else if (fl <= 29.0f) {
    emFace(); emEyes(); emSmile();

  } else {
    // warm: neutral + single drop to the right of face
    emFace(); emEyes(); emFlat();
    emDrop(EM_CX+11, EM_CY-6);
  }
}

// ── Display ───────────────────────────────────────────────────────────────────
void drawMsg(const char* line1, const char* line2 = nullptr) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(4, line2 ? 28 : 36, line1);
  if (line2) oled.drawStr(4, 44, line2);
  oled.sendBuffer();
}

void draw(const Reading& r) {
  if (!r.valid) { drawMsg("Sensor not", "detected!"); return; }

  char buf[12];
  oled.clearBuffer();

  // Header
  oled.setFont(u8g2_font_6x10_tf);
  const char* title = "Room Climate";
  oled.drawStr((128 - (int)oled.getStrWidth(title)) / 2, 10, title);
  oled.drawHLine(0, 12, 128);

  // Emoji box (right side)
  drawEmoji(r.feelsLike, r.rh);

  // Data rows — single-letter label at x=0, value centred in left 85px
  oled.setFont(u8g2_font_8x13_tf);
  const int LSEC = BOX_X - 2;  // 85
  int vw, vx;

  oled.drawStr(0, 28, "T");
  snprintf(buf, sizeof(buf), "%.1f%cC", r.tempC, '\xb0');
  vw = (int)oled.getStrWidth(buf);
  vx = (LSEC - vw) / 2; if (vx < 10) vx = 10;
  oled.drawStr(vx, 28, buf);

  oled.drawStr(0, 43, "H");
  snprintf(buf, sizeof(buf), "%.1f%%", r.rh);
  vw = (int)oled.getStrWidth(buf);
  vx = (LSEC - vw) / 2; if (vx < 10) vx = 10;
  oled.drawStr(vx, 43, buf);

  oled.drawStr(0, 58, "F");
  snprintf(buf, sizeof(buf), "%.1f%cC", r.feelsLike, '\xb0');
  vw = (int)oled.getStrWidth(buf);
  vx = (LSEC - vw) / 2; if (vx < 10) vx = 10;
  oled.drawStr(vx, 58, buf);

  oled.sendBuffer();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  bool oledOk = oled.begin();
  Serial.printf("OLED: %s\n", oledOk ? "OK" : "FAILED");
  if (oledOk) drawMsg("Starting...");

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin()) {
    Serial.println("WARN: AHT10 not found");
    sensorPresent = false;
  } else {
    sensorPresent = true;
    Serial.println("AHT10: OK");
  }

  // uint8_t relayPins[] = { RELAY_AC, RELAY_LIGHTS, RELAY_HUMIDIFIER };
  // for (int i = 0; i < 3; i++) { pinMode(relayPins[i], OUTPUT); digitalWrite(relayPins[i], HIGH); }
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  static uint32_t lastRead = 0;
  if (millis() - lastRead < READ_INTERVAL_MS) return;
  lastRead = millis();

  // Re-try sensor detection every cycle if not yet found
  if (!sensorPresent && aht.begin()) {
    sensorPresent = true;
    Serial.println("AHT10: detected");
  }

  Reading r = readSensor();
  if (r.valid)
    Serial.printf("Temp: %.1fC  RH: %.1f%%  Feels: %.1fC\n", r.tempC, r.rh, r.feelsLike);

  draw(r);
  applyRelays(r);
}
