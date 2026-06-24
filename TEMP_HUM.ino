/* The Temp_Hum code uses and esp32 and a high precision temperature and humidity sensor aht10,
 to monitor the surrounding temperature and humidity. Based on the readings and specified thresholds, specific devices (humidifiers, airconditionsers etc.) can be automated.
 * Room Climate Monitor
 * Board  : ESP32-WROOM
 * Sensor : AHT10  — I2C : D21=SDA, D22=SCL
 * Display: SH1106 — SPI : D18=CLK, D23=MOSI, D15=RST, D4=DC, D5=CS
 *
 * Libraries: U8g2 (oliver), Adafruit AHTX0, Adafruit BusIO, ArduinoJson
 *
 * WiFi setup:
 *   1. Set WIFI_SSID and WIFI_PASS below
 *   2. Run server.py on your PC — it will print the IP to put in PC_HOST
 *   3. Upload this sketch; Serial Monitor shows ESP32's own IP
 *
 * Relay control: uncomment blocks in applyRelays() + pinMode block in setup()
 */

#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_AHTX0.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ── WiFi / network — CONFIGURE THESE ─────────────────────────────────────────
#define WIFI_SSID  "Baali_air"
#define WIFI_PASS  " "
#define PC_HOST    ""   // your PC's local IP (printed by server.py)
#define PC_PORT    5000

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

// ── Layout constants (live display) ──────────────────────────────────────────
#define BOX_X  87
#define BOX_Y  14
#define BOX_W  40
#define BOX_H  49
#define EM_CX  107
#define EM_CY   38

// ── Display mode durations ────────────────────────────────────────────────────
const uint32_t DUR_LIVE       = 30000;   // 30 s live feed
const uint32_t DUR_TEMP_GRAPH = 15000;   // 15 s temperature graph
const uint32_t DUR_HUM_GRAPH  = 15000;   // 15 s humidity graph
const uint32_t READ_INTERVAL  = 2000;    // sensor poll interval

// ── Automation thresholds ─────────────────────────────────────────────────────
struct AutoConfig { float acOnTemp, acOffTemp, humOnRH, humOffRH; };
AutoConfig cfg = { 28.0f, 25.0f, 40.0f, 60.0f };

// ── State structs ─────────────────────────────────────────────────────────────
struct RelayState { bool ac, lights, humidifier; };
struct Reading    { float tempC, rh, feelsLike; bool valid; };
enum   DispMode   { LIVE=0, TEMP_GRAPH=1, HUM_GRAPH=2 };

// ── Hardware objects ──────────────────────────────────────────────────────────
U8G2_SH1106_128X64_NONAME_F_4W_HW_SPI oled(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
Adafruit_AHTX0 aht;
WebServer      server(80);

// ── Runtime state ─────────────────────────────────────────────────────────────
RelayState relay      = {false,false,false};
bool  sensorPresent   = false;
Reading curReading    = {0,0,0,false};
DispMode dispMode     = LIVE;
uint32_t modeSince    = 0;
uint32_t lastReadMs   = 0;
uint32_t lastWifiMs   = 0;
uint32_t lastSensChk  = 0;
uint32_t histFetchedMs= 0;

// ── History buffers (96 points = one per 15 min for 24 h) ────────────────────
#define HIST_MAX 96
float histTemp[HIST_MAX];
float histHum[HIST_MAX];
int   histCount = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Heat-index (NOAA Steadman, Celsius)
// ─────────────────────────────────────────────────────────────────────────────
float heatIndex(float t, float h) {
  if (t < 27.0f) return t;
  return -8.78469475556f + 1.61139411f*t + 2.33854883889f*h
         - 0.14611605f*t*h  - 0.012308094f*t*t - 0.016424828f*h*h
         + 0.002211732f*t*t*h + 0.00072546f*t*h*h - 0.000003582f*t*t*h*h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensor
// ─────────────────────────────────────────────────────────────────────────────
Reading readSensor() {
  if (!sensorPresent) return {0,0,0,false};
  sensors_event_t hev, tev;
  if (!aht.getEvent(&hev, &tev)) return {0,0,0,false};
  float t = tev.temperature, h = hev.relative_humidity;
  return {t, h, heatIndex(t,h), true};
}

// ─────────────────────────────────────────────────────────────────────────────
// Relay control (uncomment blocks when devices are wired)
// ─────────────────────────────────────────────────────────────────────────────
void applyRelays(const Reading& r) {
  if (!r.valid) return;
  // if (r.tempC > cfg.acOnTemp  && !relay.ac)      { digitalWrite(RELAY_AC, LOW);  relay.ac=true; }
  // if (r.tempC < cfg.acOffTemp &&  relay.ac)       { digitalWrite(RELAY_AC, HIGH); relay.ac=false; }
  // if (r.rh < cfg.humOnRH  && !relay.humidifier)  { digitalWrite(RELAY_HUMIDIFIER, LOW);  relay.humidifier=true; }
  // if (r.rh > cfg.humOffRH &&  relay.humidifier)  { digitalWrite(RELAY_HUMIDIFIER, HIGH); relay.humidifier=false; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Emoji primitives (live display)
// ─────────────────────────────────────────────────────────────────────────────
static void emFace()  { oled.drawCircle(EM_CX,EM_CY,12); }
static void emEyes()  { oled.drawDisc(EM_CX-5,EM_CY-4,2); oled.drawDisc(EM_CX+5,EM_CY-4,2); }
static void emSmile() { oled.drawCircle(EM_CX,EM_CY+4, 6,U8G2_DRAW_LOWER_LEFT|U8G2_DRAW_LOWER_RIGHT); }
static void emFrown() { oled.drawCircle(EM_CX,EM_CY+11,6,U8G2_DRAW_UPPER_LEFT|U8G2_DRAW_UPPER_RIGHT); }
static void emFlat()  { oled.drawHLine(EM_CX-4,EM_CY+5,9); }
static void emDrop(int x,int y){ oled.drawPixel(x,y); oled.drawDisc(x,y+3,2); }

static void drawEmoji(float fl, float rh) {
  oled.drawFrame(BOX_X,BOX_Y,BOX_W,BOX_H);
  if (fl < 22.0f) {
    emFace(); emEyes(); emFrown();
    int sx=EM_CX, sy=BOX_Y+6;
    oled.drawHLine(sx-4,sy,9); oled.drawVLine(sx,sy-4,9);
    oled.drawLine(sx-3,sy-3,sx+3,sy+3); oled.drawLine(sx+3,sy-3,sx-3,sy+3);
  } else if (fl > 29.0f) {
    emFace();
    oled.drawHLine(EM_CX-8,EM_CY-4,6); oled.drawHLine(EM_CX+2,EM_CY-4,6);
    oled.drawEllipse(EM_CX,EM_CY+5,4,3,U8G2_DRAW_ALL);
    for (int i=0;i<3;i++){int wx=EM_CX-6+i*6,wy=BOX_Y+2;oled.drawLine(wx,wy+3,wx+2,wy);oled.drawLine(wx+2,wy,wx+4,wy+3);}
  } else if (rh > 65.0f) {
    emFace(); emEyes(); emFlat();
    emDrop(EM_CX-8,EM_CY+13); emDrop(EM_CX,EM_CY+13); emDrop(EM_CX+8,EM_CY+13);
  } else if (rh < 40.0f) {
    emFace(); emEyes(); emFlat();
    oled.drawLine(EM_CX-1,EM_CY-8,EM_CX+2,EM_CY-2); oled.drawLine(EM_CX+2,EM_CY-2,EM_CX-1,EM_CY+4);
  } else if (fl <= 29.0f) {
    emFace(); emEyes(); emSmile();
  } else {
    emFace(); emEyes(); emFlat(); emDrop(EM_CX+11,EM_CY-6);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Live display
// ─────────────────────────────────────────────────────────────────────────────
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
  oled.setFont(u8g2_font_6x10_tf);
  const char* title = "Room Climate";
  oled.drawStr((128-(int)oled.getStrWidth(title))/2, 10, title);
  oled.drawHLine(0,12,128);
  drawEmoji(r.feelsLike, r.rh);
  oled.setFont(u8g2_font_8x13_tf);
  const int LSEC = BOX_X-2;
  int vw, vx;
  oled.drawStr(0,28,"T");
  snprintf(buf,sizeof(buf),"%.1f%cC",r.tempC,'\xb0');
  vw=(int)oled.getStrWidth(buf); vx=(LSEC-vw)/2; if(vx<10)vx=10;
  oled.drawStr(vx,28,buf);
  oled.drawStr(0,43,"H");
  snprintf(buf,sizeof(buf),"%.1f%%",r.rh);
  vw=(int)oled.getStrWidth(buf); vx=(LSEC-vw)/2; if(vx<10)vx=10;
  oled.drawStr(vx,43,buf);
  oled.drawStr(0,58,"F");
  snprintf(buf,sizeof(buf),"%.1f%cC",r.feelsLike,'\xb0');
  vw=(int)oled.getStrWidth(buf); vx=(LSEC-vw)/2; if(vx<10)vx=10;
  oled.drawStr(vx,58,buf);
  oled.sendBuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
// 24h graph renderer
// ─────────────────────────────────────────────────────────────────────────────
static void drawGraph(float* vals, int count, const char* title,
                      float curVal, const char* unit) {
  oled.clearBuffer();

  if (count < 2) {
    oled.setFont(u8g2_font_6x10_tf);
    drawMsg("No history", "yet...");
    return;
  }

  // Header: title left, current value right
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(0, 7, title);
  char cur[10];
  snprintf(cur, sizeof(cur), "%.1f%s", curVal, unit);
  oled.drawStr(127-(int)oled.getStrWidth(cur), 7, cur);
  oled.drawHLine(0, 9, 128);

  // Graph area: x=[22,127] y=[11,57] → w=106, h=46
  const int GX=22, GY=11, GW=106, GH=46;

  // Dynamic range with 10 % padding
  float lo=vals[0], hi=vals[0];
  for (int i=1;i<count;i++) { if(vals[i]<lo)lo=vals[i]; if(vals[i]>hi)hi=vals[i]; }
  float pad=(hi-lo)*0.1f; if(pad<0.5f)pad=0.5f;
  lo-=pad; hi+=pad;
  float range=hi-lo; if(range<0.1f)range=0.1f;

  // Axes
  oled.drawVLine(GX, GY, GH+1);
  oled.drawHLine(GX, GY+GH, GW);

  // Y-axis labels
  char buf[8];
  snprintf(buf,sizeof(buf),"%.0f",hi); oled.drawStr(0,GY+6,buf);
  snprintf(buf,sizeof(buf),"%.0f",lo); oled.drawStr(0,GY+GH,buf);

  // X-axis labels
  oled.drawStr(GX+1, 63, "24h");
  const char* nl="now";
  oled.drawStr(GX+GW-(int)oled.getStrWidth(nl), 63, nl);

  // Line graph
  for (int i=1;i<count;i++) {
    int x1=GX+1+(int)((long)(i-1)*(GW-1)/(count-1));
    int x2=GX+1+(int)((long)(i)  *(GW-1)/(count-1));
    int y1=GY+GH-1-(int)((vals[i-1]-lo)/range*(GH-1));
    int y2=GY+GH-1-(int)((vals[i]  -lo)/range*(GH-1));
    oled.drawLine(x1,constrain(y1,GY,GY+GH-1),x2,constrain(y2,GY,GY+GH-1));
  }
  oled.sendBuffer();
}

void drawTempGraph() { drawGraph(histTemp,histCount,"Temp 24h", curReading.tempC, "\xb0""C"); }
void drawHumGraph()  { drawGraph(histHum, histCount,"Humi 24h", curReading.rh,    "%");       }

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi connecting");
  for (int i=0; i<20 && WiFi.status()!=WL_CONNECTED; i++) {
    delay(500); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK  IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.println("mDNS: http://climate.local");
  } else {
    Serial.println("\nWiFi failed — running offline");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Web server handlers (served by ESP32)
// ─────────────────────────────────────────────────────────────────────────────
static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html><html>
<head><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Room Climate</title>
<style>
body{background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:30px;margin:0}
h2{color:#4af;margin-bottom:20px}
.g{display:flex;justify-content:center;gap:16px;flex-wrap:wrap}
.c{background:#1e1e1e;border:1px solid #333;border-radius:12px;padding:20px 28px}
.v{font-size:2.2em;font-weight:700;color:#4f4}
.l{color:#888;font-size:.8em;margin-top:6px}
p{color:#555;font-size:.75em;margin-top:24px}
</style></head>
<body>
<h2>Room Climate Monitor</h2>
<div class='g' id='d'><div class='c'><div class='v'>--</div><div class='l'>Loading...</div></div></div>
<p>Updates every 5 s</p>
<script>
async function u(){
  try{
    const d=await(await fetch('/data.json')).json();
    document.getElementById('d').innerHTML=d.valid
      ?`<div class='c'><div class='v'>${d.temp.toFixed(1)}&deg;C</div><div class='l'>Temperature</div></div>
        <div class='c'><div class='v'>${d.humidity.toFixed(1)}%</div><div class='l'>Humidity</div></div>
        <div class='c'><div class='v'>${d.feelsLike.toFixed(1)}&deg;C</div><div class='l'>Feels Like</div></div>`
      :'<div class=\'c\'><div class=\'v\' style=\'color:#f44\'>Sensor error</div></div>';
  }catch(e){}
}
u();setInterval(u,5000);
</script></body></html>
)html";

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleDataJson() {
  char json[128];
  snprintf(json, sizeof(json),
    "{\"temp\":%.2f,\"humidity\":%.2f,\"feelsLike\":%.2f,\"valid\":%s}",
    curReading.tempC, curReading.rh, curReading.feelsLike,
    curReading.valid ? "true" : "false");
  server.send(200, "application/json", json);
}

void handle404() {
  server.send(404, "text/plain", "Not found");
}

// ─────────────────────────────────────────────────────────────────────────────
// Fetch 24h history from PC's Python server
// ─────────────────────────────────────────────────────────────────────────────
void fetchHistory() {
  if (WiFi.status() != WL_CONNECTED) return;

  drawMsg("Fetching", "history...");

  HTTPClient http;
  String url = "http://";
  url += PC_HOST;
  url += ":";
  url += PC_PORT;
  url += "/history.json";

  http.begin(url);
  http.setTimeout(4000);
  int code = http.GET();

  if (code == 200) {
    String payload = http.getString();
    // ArduinoJson v6: DynamicJsonDocument doc(2048);
    // ArduinoJson v7: JsonDocument doc;
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonArray temps = doc["temps"];
      JsonArray hums  = doc["hums"];
      histCount = min((int)min(temps.size(), hums.size()), HIST_MAX);
      for (int i = 0; i < histCount; i++) {
        histTemp[i] = temps[i].as<float>();
        histHum[i]  = hums[i].as<float>();
      }
      Serial.printf("History: %d points loaded\n", histCount);
    } else {
      Serial.println("History: JSON parse error");
    }
  } else {
    Serial.printf("History: HTTP %d (is server.py running?)\n", code);
  }
  http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // Display
  bool oledOk = oled.begin();
  Serial.printf("OLED: %s\n", oledOk ? "OK" : "FAILED");
  if (oledOk) drawMsg("Starting...");

  // Sensor
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!aht.begin()) {
    Serial.println("WARN: AHT10 not found");
  } else {
    sensorPresent = true;
    Serial.println("AHT10: OK");
  }

  // WiFi + mDNS
  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    if (MDNS.begin("climate")) Serial.println("mDNS ready");
    server.on("/",          handleRoot);
    server.on("/data.json", handleDataJson);
    server.onNotFound(handle404);
    server.begin();
    Serial.println("HTTP server started");
  }

  // Relay GPIO — uncomment when wired
  // uint8_t rPins[]={RELAY_AC,RELAY_LIGHTS,RELAY_HUMIDIFIER};
  // for(int i=0;i<3;i++){pinMode(rPins[i],OUTPUT);digitalWrite(rPins[i],HIGH);}

  modeSince = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // Serve web requests
  if (WiFi.status() == WL_CONNECTED) server.handleClient();

  // WiFi reconnect check (every 30 s)
  if (now - lastWifiMs > 30000) {
    lastWifiMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      Serial.println("WiFi: reconnecting...");
    }
  }

  // Sensor read (every 2 s)
  if (now - lastReadMs > READ_INTERVAL) {
    lastReadMs = now;

    // AHT10 reconnect attempt (every 5 s when absent)
    if (!sensorPresent && now - lastSensChk > 5000) {
      lastSensChk = now;
      if (aht.begin()) { sensorPresent = true; Serial.println("AHT10: detected"); }
    }

    curReading = readSensor();
    if (curReading.valid)
      Serial.printf("T:%.1fC  H:%.1f%%  F:%.1fC\n",
                    curReading.tempC, curReading.rh, curReading.feelsLike);
    applyRelays(curReading);

    if (dispMode == LIVE) draw(curReading);
  }

  // Display mode state machine
  uint32_t dur = (dispMode==LIVE) ? DUR_LIVE :
                 (dispMode==TEMP_GRAPH) ? DUR_TEMP_GRAPH : DUR_HUM_GRAPH;

  if (now - modeSince > dur) {
    dispMode = (DispMode)((dispMode + 1) % 3);
    modeSince = now;

    if (dispMode == TEMP_GRAPH) {
      // Fetch fresh history if empty or older than 5 min
      if (histCount == 0 || now - histFetchedMs > 300000UL) {
        fetchHistory();
        histFetchedMs = now;
      }
      drawTempGraph();
    } else if (dispMode == HUM_GRAPH) {
      drawHumGraph();
    } else {
      draw(curReading);  // immediate redraw entering live mode
    }
  }
}
