// Water Tank Monitor (final) for NodeMCU ESP8266 + HC-SR04 + 16x2 I2C LCD
// Features: LCD level bar, detailed phone web page, Telegram alerts + /level,
//           sensor error detection (no more stuck readings).
//
// Libraries (Arduino IDE > Library Manager):
//   - LiquidCrystal I2C (Frank de Brabander)
//   - UniversalTelegramBot (Brian Lough)
//   - ArduinoJson (Benoit Blanchon)  -> version 6.x
//
// Wiring:
//   LCD     GND -> G     VCC -> VU (5V)   SDA -> D2   SCL -> D1
//   HC-SR04 VCC -> VU (5V)  GND -> G      Trig -> D5  Echo -> D6

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ---------- EDIT THESE ----------
const char *WIFI_SSID = "wifi name";
const char *WIFI_PASS = "password";

#define BOT_TOKEN "bot token"   // e.g. 1234567890:AAbb... (no link)
#define CHAT_ID   "chat id"

const float EMPTY_CM = 120.0;   // sensor-to-bottom distance when tank is EMPTY
const float FULL_CM  = 10.0;    // sensor-to-water distance when tank is FULL
const float TANK_LITRES = 1000; // total tank capacity in litres (set yours)

const int LOW_PERCENT  = 20;    // "low" alert level
const int FULL_PERCENT = 95;    // "full" alert level
// --------------------------------

LiquidCrystal_I2C lcd(0x27, 16, 2);   // try 0x3F if blank
ESP8266WebServer server(80);
BearSSL::WiFiClientSecure secured;
UniversalTelegramBot bot(BOT_TOKEN, secured);

const int TRIG_PIN = 14;   // D5
const int ECHO_PIN = 12;   // D6

float distanceCm = 0;
int levelPercent = 0;
int prevPercent = 0;
String trend = "steady";
bool haveReading = false;

int failCount = 0;
bool sensorError = false;
unsigned long lastGoodMs = 0;

unsigned long lastRead = 0;
unsigned long lastBotCheck = 0;
unsigned long lastTrendMs = 0;

bool lowAlertSent = false;
bool fullAlertSent = false;
bool errorAlertSent = false;

// ---------- Sensor ----------
float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return -1;                 // no echo
  float d = dur * 0.0343 / 2.0;
  if (d < 3.0) return -1;                  // too close / noise
  return d;
}

// Median of the valid readings out of 7. Returns -1 if fewer than 4 are valid.
float robustDistance() {
  float v[7];
  int n = 0;
  for (int i = 0; i < 7; i++) {
    float d = readDistanceCm();
    if (d > 0) v[n++] = d;
    delay(25);
  }
  if (n < 4) return -1;
  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (v[j] < v[i]) { float t = v[i]; v[i] = v[j]; v[j] = t; }
  return v[n / 2];
}

void updateLevel() {
  float d = robustDistance();
  if (d < 0) {                              // sensor gave nothing usable
    if (++failCount >= 5) sensorError = true;
    return;
  }
  failCount = 0;
  sensorError = false;
  lastGoodMs = millis();
  haveReading = true;
  distanceCm = d;
  float p = (EMPTY_CM - d) / (EMPTY_CM - FULL_CM) * 100.0;
  levelPercent = constrain((int)(p + 0.5), 0, 100);

  // Trend: compare with level one minute ago
  if (millis() - lastTrendMs > 60000UL) {
    int diff = levelPercent - prevPercent;
    trend = diff >= 2 ? "filling" : (diff <= -2 ? "draining" : "steady");
    prevPercent = levelPercent;
    lastTrendMs = millis();
  }
}

float waterHeightCm() {
  return constrain(EMPTY_CM - distanceCm, 0, EMPTY_CM - FULL_CM);
}

String statusText() {
  if (sensorError) return "SENSOR ERROR";
  if (!haveReading) return "STARTING";
  if (levelPercent <= LOW_PERCENT) return "LOW";
  if (levelPercent >= FULL_PERCENT) return "FULL";
  return "OK";
}

// ---------- LCD ----------
void drawLcd() {
  if (sensorError) {
    lcd.setCursor(0, 0); lcd.print("Sensor error!   ");
    lcd.setCursor(0, 1); lcd.print("No echo - check ");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("Water Level:    ");
  lcd.setCursor(0, 1);
  lcd.print("[");
  int filled = map(levelPercent, 0, 100, 0, 8);
  for (int i = 0; i < 8; i++) lcd.print(i < filled ? (char)255 : ' ');
  lcd.print("] ");
  if (levelPercent < 100) lcd.print(" ");
  if (levelPercent < 10) lcd.print(" ");
  lcd.print(levelPercent);
  lcd.print("%");
}

// ---------- Web page ----------
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Water Tank</title>
<style>
  :root{--bg:#0b1d2e;--card:#122c44;--text:#e8f4ff;--dim:#8fb3d1}
  *{box-sizing:border-box}
  body{margin:0;padding:20px 16px 32px;background:var(--bg);color:var(--text);
       font-family:system-ui,sans-serif;display:flex;flex-direction:column;align-items:center}
  h1{font-size:18px;font-weight:600;margin:0 0 14px;color:var(--dim);letter-spacing:.5px}
  .tank{position:relative;width:150px;height:240px;border:4px solid #8fc7ff;border-radius:14px;
        overflow:hidden;background:#102a43}
  .water{position:absolute;bottom:0;left:0;right:0;height:0%;
         background:linear-gradient(#4aa8ff,#1c6fd1);transition:height 1s ease}
  .pct{font-size:54px;font-weight:700;margin:14px 0 4px}
  .badge{padding:4px 14px;border-radius:999px;font-weight:700;font-size:14px;background:#1f7a3d}
  .badge.LOW{background:#c0392b}.badge.FULL{background:#2874c9}
  .badge.ERR{background:#b8860b}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;width:100%;max-width:420px;margin-top:20px}
  .card{background:var(--card);border-radius:12px;padding:12px}
  .card .k{font-size:12px;color:var(--dim)}
  .card .v{font-size:20px;font-weight:600;margin-top:4px}
  .foot{margin-top:16px;font-size:12px;color:var(--dim);text-align:center}
</style></head><body>
<h1>WATER TANK MONITOR</h1>
<div class="tank"><div class="water" id="w"></div></div>
<div class="pct" id="p">--%</div>
<div class="badge" id="b">connecting</div>
<div class="grid">
  <div class="card"><div class="k">Water height</div><div class="v" id="h">--</div></div>
  <div class="card"><div class="k">Volume (est.)</div><div class="v" id="l">--</div></div>
  <div class="card"><div class="k">Distance from sensor</div><div class="v" id="d">--</div></div>
  <div class="card"><div class="k">Trend (1 min)</div><div class="v" id="t">--</div></div>
  <div class="card"><div class="k">Sensor</div><div class="v" id="s">--</div></div>
  <div class="card"><div class="k">Last good reading</div><div class="v" id="a">--</div></div>
  <div class="card"><div class="k">Wi-Fi signal</div><div class="v" id="r">--</div></div>
  <div class="card"><div class="k">Uptime</div><div class="v" id="u">--</div></div>
</div>
<div class="foot" id="f"></div>
<script>
function fmtUp(s){const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
  return (d?d+'d ':'')+h+'h '+m+'m';}
function ago(s){return s<60?s+' s ago':Math.floor(s/60)+' min ago';}
async function tick(){
  try{
    const r=await fetch('/level'); const j=await r.json();
    document.getElementById('w').style.height=j.percent+'%';
    document.getElementById('p').textContent=j.percent+'%';
    const b=document.getElementById('b');
    b.textContent=j.status;
    b.className='badge '+(j.status==='LOW'?'LOW':j.status==='FULL'?'FULL':j.status==='SENSOR ERROR'?'ERR':'');
    document.getElementById('h').textContent=j.height.toFixed(1)+' cm';
    document.getElementById('l').textContent=Math.round(j.litres)+' L';
    document.getElementById('d').textContent=j.distance.toFixed(1)+' cm';
    document.getElementById('t').textContent=j.trend;
    document.getElementById('s').textContent=j.error?'No echo':'Working';
    document.getElementById('a').textContent=ago(j.ago);
    document.getElementById('r').textContent=j.rssi+' dBm';
    document.getElementById('u').textContent=fmtUp(j.uptime);
    document.getElementById('f').textContent='Empty = '+j.emptyCm+' cm, Full = '+j.fullCm+' cm from sensor';
  }catch(e){document.getElementById('b').textContent='offline';}
}
tick(); setInterval(tick,2000);
</script></body></html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleLevel() {
  unsigned long agoSec = haveReading ? (millis() - lastGoodMs) / 1000 : 0;
  String j = "{";
  j += "\"percent\":" + String(levelPercent);
  j += ",\"distance\":" + String(distanceCm, 1);
  j += ",\"height\":" + String(waterHeightCm(), 1);
  j += ",\"litres\":" + String(levelPercent / 100.0 * TANK_LITRES, 0);
  j += ",\"status\":\"" + statusText() + "\"";
  j += ",\"trend\":\"" + trend + "\"";
  j += ",\"error\":" + String(sensorError ? "true" : "false");
  j += ",\"ago\":" + String(agoSec);
  j += ",\"rssi\":" + String(WiFi.RSSI());
  j += ",\"uptime\":" + String(millis() / 1000);
  j += ",\"emptyCm\":" + String(EMPTY_CM, 0);
  j += ",\"fullCm\":" + String(FULL_CM, 0);
  j += "}";
  server.send(200, "application/json", j);
}

// ---------- Telegram ----------
String levelMessage() {
  if (sensorError) return "Sensor error: no echo from the ultrasonic sensor. Check it.";
  String m = "Tank level: " + String(levelPercent) + "% (" + statusText() + ")\n";
  m += "Water height: " + String(waterHeightCm(), 1) + " cm\n";
  m += "Volume: ~" + String(levelPercent / 100.0 * TANK_LITRES, 0) + " L\n";
  m += "Distance from sensor: " + String(distanceCm, 1) + " cm\n";
  m += "Trend: " + trend;
  if (levelPercent <= LOW_PERCENT) m += "\nLow! Time to switch the pump on.";
  else if (levelPercent >= FULL_PERCENT) m += "\nTank is full.";
  return m;
}

void handleTelegram() {
  int n = bot.getUpdates(bot.last_message_received + 1);
  while (n) {
    for (int i = 0; i < n; i++) {
      String chatId = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;
      if (chatId != CHAT_ID) {
        bot.sendMessage(chatId, "Not authorized.", "");
        continue;
      }
      if (text == "/level" || text == "/status") {
        bot.sendMessage(CHAT_ID, levelMessage(), "");
      } else if (text == "/start" || text == "/help") {
        bot.sendMessage(CHAT_ID,
          "Water tank bot\n/level - current water level\n"
          "Alerts: low at " + String(LOW_PERCENT) + "%, full at " +
          String(FULL_PERCENT) + "%, and sensor errors.", "");
      }
    }
    n = bot.getUpdates(bot.last_message_received + 1);
  }
}

void checkAlerts() {
  if (sensorError) {
    if (!errorAlertSent) {
      bot.sendMessage(CHAT_ID, "SENSOR ERROR: no echo from the sensor. Level readings are not reliable.", "");
      errorAlertSent = true;
    }
    return;
  }
  errorAlertSent = false;

  if (levelPercent <= LOW_PERCENT && !lowAlertSent) {
    bot.sendMessage(CHAT_ID, "LOW WATER: " + String(levelPercent) + "%", "");
    lowAlertSent = true;
  }
  if (levelPercent >= LOW_PERCENT + 10) lowAlertSent = false;

  if (levelPercent >= FULL_PERCENT && !fullAlertSent) {
    bot.sendMessage(CHAT_ID, "TANK FULL: " + String(levelPercent) + "%", "");
    fullAlertSent = true;
  }
  if (levelPercent <= FULL_PERCENT - 10) fullAlertSent = false;
}

// ---------- Setup / loop ----------
void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  lcd.init();
  lcd.backlight();
  lcd.print("Connecting WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(300);

  secured.setInsecure();
  secured.setBufferSizes(1024, 1024);

  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("Local page IP:");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
  } else {
    lcd.print("No WiFi - LCD");
    lcd.setCursor(0, 1);
    lcd.print("only mode");
  }
  delay(4000);
  lcd.clear();

  server.on("/", handleRoot);
  server.on("/level", handleLevel);
  server.begin();

  updateLevel();
  prevPercent = levelPercent;
  lastTrendMs = millis();
  if (WiFi.status() == WL_CONNECTED) {
    bot.sendMessage(CHAT_ID, "Tank monitor online.\n" + levelMessage(), "");
  }
}

void loop() {
  server.handleClient();

  if (millis() - lastRead > 1000) {
    lastRead = millis();
    updateLevel();
    drawLcd();
    if (WiFi.status() == WL_CONNECTED) checkAlerts();
  }

  if (WiFi.status() == WL_CONNECTED && millis() - lastBotCheck > 3000) {
    lastBotCheck = millis();
    handleTelegram();
  }
}
