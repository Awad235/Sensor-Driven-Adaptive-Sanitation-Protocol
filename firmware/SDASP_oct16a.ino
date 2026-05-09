#include <DHT.h>
#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>          // NTP / POSIX time — built into ESP32 Arduino core

// ── WiFi ──
const char* WIFI_SSID = "R03-F554";
const char* WIFI_PASS = "auacb96368";

// ── NTP / IST ──
// IST = UTC+5:30 → offset = 5*3600 + 30*60 = 19800 seconds, no DST
#define NTP_SERVER  "pool.ntp.org"
#define UTC_OFFSET  19800   // seconds
#define DST_OFFSET  0

WebServer server(80);

// ── Pins ──
#define IR_PIN      35
#define DHT_PIN     32
#define RELAY_PIN   33   // Active LOW (JQC3F-05VDC-C) 3.3V logic
#define BUZZER_PIN  26   // Active LOW buzzer
#define DHT_TYPE    DHT22
#define NH3_PIN     36   // VP — ADC1 analog input
#define H2S_PIN     39   // VN — ADC1 analog input

// ── Gas sensor calibration — hardcoded from calibration run ──
// H2S: baseline=0.032V  trigger=0.065V
// NH3: baseline=0.054V  trigger=0.100V
// OLD:
#define NH3_BASELINE  0.1f
#define H2S_BASELINE  0.010f
#define NH3_TRIGGER   0.176f
#define H2S_TRIGGER   0.016f

// NEW:
#define NH3_BASELINE  0.1f      // update after recal
#define H2S_BASELINE  0.3f      // update after recal — 0.010V was noise floor
#define NH3_RL        1000000.0f  // 1MΩ from SEN0567 schematic
#define H2S_RL        3000.0f     // 3kΩ from SEN0568 schematic
#define NH3_SLOPE     -0.48f      // from GM-802B datasheet Fig3
#define H2S_SLOPE     -0.55f      // from GM-602B datasheet Fig3
#define BASELINES_OK  true   // always valid — values are hardcoded

// ── Alarm thresholds ──
#define MAX_OCCUPANCY   20
#define HUM_THRESHOLD   70.0f
#define NH3_THRESHOLD   5.0f
#define H2S_THRESHOLD   3.0f
#define COOLDOWN_MS     300000UL  // 5 minutes

// ── LCD page cycling ──
#define LCD_PAGE_MS  4000
int           lcdPage        = 0;
unsigned long lastPageSwitch = 0;
String        lcdLine0       = "";
String        lcdLine1       = "";

// ── Keypad ──
const String VALID_CODES[] = {"1234", "5678"};
const int    NUM_CODES     = 2;

const byte ROWS = 4, COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {23, 19, 18, 5};
byte colPins[COLS] = {15, 25, 4};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT_TYPE);

// ── Alarm types ──
// Each type drives a different buzzer pattern (non-blocking)
//
//   HUMIDITY  → long interval: 200ms ON, 5000ms OFF  (slow, lazy beep)
//   OCCUPANCY → mid interval:  200ms ON, 2000ms OFF  (moderate beep)
//   GAS       → continuous:    200ms ON,  100ms OFF  (rapid, urgent beep)
//   NONE      → buzzer silent

enum AlarmType { ALARM_NONE, ALARM_HUMIDITY, ALARM_OCCUPANCY, ALARM_GAS };

// Buzzer pattern table  [ON ms, OFF ms]
const unsigned long BUZZER_ON_MS[]  = {0,  200, 200, 200};
const unsigned long BUZZER_OFF_MS[] = {0, 5000, 2000, 100};

// ── State ──
int           irCount        = 0;
int           occupancy      = 0;
bool          lastIRState    = HIGH;
bool          alarmActive    = false;
bool          inCooldown     = false;
unsigned long cooldownStart  = 0;
unsigned long lastIRTime     = 0;
String        enteredCode    = "";
String        alarmReason    = "";
AlarmType     currentAlarm   = ALARM_NONE;
float         lastHum        = 0;
float         lastTemp       = 0;
float         lastNH3        = 0;
float         lastH2S        = 0;
String        lastUsedCode   = "";
String        lastAckTime    = "";

// ── Gas alarm latch ──
// Stays true once a gas alarm fires so the alarm cannot self-cancel
// if readings drop back below threshold mid-alarm.
// Only cleared by a valid keypad code (silenceAlarm) or after cooldown ends.
bool          gasAlarmLatched = false;

// ── Non-blocking buzzer state ──
bool          buzzerOn       = false;
unsigned long buzzerToggleAt = 0;   // millis() when to flip buzzer state next

// ── Non-blocking LCD message ──
unsigned long lcdMsgUntil    = 0;
bool          lcdMsgActive   = false;

// ════════════════════════════════════
// HELPERS
// ════════════════════════════════════

void lcdWrite(String line0, String line1) {
  while (line0.length() < 16) line0 += " ";
  while (line1.length() < 16) line1 += " ";
  if (line0 != lcdLine0) {
    lcd.setCursor(0, 0);
    lcd.print(line0);
    lcdLine0 = line0;
  }
  if (line1 != lcdLine1) {
    lcd.setCursor(0, 1);
    lcd.print(line1);
    lcdLine1 = line1;
  }
}

float readVolts(int pin) {
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  return (sum / 16.0f) * (3.3f / 4095.0f);
}

// NEW — replace with:
float toRs(float vout, float RL) {
  if (vout <= 0.01f) return RL * 999.0f;
  return RL * (3.3f - vout) / vout;
}

float toPPM(float vout, float base_v, float RL, float slope, float pmin, float pmax) {
  float sensorR  = toRs(vout,   RL);   // renamed Rs → sensorR
  float cleanR   = toRs(base_v, RL);   // renamed R0 → cleanR
  if (cleanR <= 0 || sensorR <= 0) return pmin;
  float ratio = sensorR / cleanR;
  return constrain(pow(ratio, 1.0f / slope), pmin, pmax);
}

// NEW — zero-based percentage, 100% = alarm level:
int pctOfThreshold(float ppm, float threshold, float floor_ppm) {
  float adjusted = ppm - floor_ppm;
  float range    = threshold - floor_ppm;
  if (adjusted <= 0 || range <= 0) return 0;
  return (int)constrain((adjusted / range) * 100.0f, 0.0f, 999.0f);
}

bool isValidCode(String code) {
  for (int i = 0; i < NUM_CODES; i++) {
    if (code == VALID_CODES[i]) return true;
  }
  return false;
}

// Returns current IST wall-clock as "HH:MM:SS DD/MM/YYYY"
// Falls back to uptime string if NTP hasn't synced yet.
String getISTTime() {
  struct tm t;
  if (!getLocalTime(&t, 100)) {          // 100ms timeout
    // NTP not ready — fall back to uptime
    unsigned long s = millis() / 1000;
    char buf[20];
    sprintf(buf, "up %02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
    return String(buf);
  }
  char buf[22];
  // Format: HH:MM:SS DD/MM/YYYY
  sprintf(buf, "%02d:%02d:%02d %02d/%02d/%04d",
          t.tm_hour, t.tm_min, t.tm_sec,
          t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  return String(buf);
}

// ════════════════════════════════════
// NON-BLOCKING BUZZER DRIVER
// Call every loop iteration while alarm is active.
// ════════════════════════════════════

void buzzerTick() {
  if (!alarmActive || currentAlarm == ALARM_NONE) return;

  if (millis() >= buzzerToggleAt) {
    if (buzzerOn) {
      // Turn OFF, schedule next ON
      digitalWrite(BUZZER_PIN, HIGH);  // OFF (active LOW)
      buzzerOn       = false;
      buzzerToggleAt = millis() + BUZZER_OFF_MS[currentAlarm];
    } else {
      // Turn ON, schedule next OFF
      digitalWrite(BUZZER_PIN, LOW);   // ON (active LOW)
      buzzerOn       = true;
      buzzerToggleAt = millis() + BUZZER_ON_MS[currentAlarm];
    }
  }
}

void buzzerStop() {
  digitalWrite(BUZZER_PIN, HIGH);  // OFF
  buzzerOn       = false;
  buzzerToggleAt = 0;
}

void buzzerStart() {
  // Kick off first ON pulse immediately
  digitalWrite(BUZZER_PIN, LOW);   // ON
  buzzerOn       = true;
  buzzerToggleAt = millis() + BUZZER_ON_MS[currentAlarm];
}

// ════════════════════════════════════
// ALARM
// ════════════════════════════════════

void triggerAlarm(String reason, AlarmType type) {
  if (alarmActive || inCooldown) return;
  alarmActive   = true;
  alarmReason   = reason;
  currentAlarm  = type;

  digitalWrite(RELAY_PIN, LOW);    // Fan ON (active LOW)
  buzzerStart();

  Serial.println("⚠ ALARM [" + String(type) + "]: " + reason);
  lcdLine0 = "";
  lcdLine1 = "";
  lcdWrite("!! ALARM !!", reason.substring(0, 16));
  delay(1000);
  lcdLine0 = "";
  lcdLine1 = "";
  lcdWrite("Enter Code:", "____");
}

void silenceAlarm(String code) {
  alarmActive     = false;
  inCooldown      = true;
  cooldownStart   = millis();
  lastUsedCode    = code;
  gasAlarmLatched = false;   // unlatch gas alarm — valid code entered

  lastAckTime = getISTTime();   // real IST wall-clock (or uptime fallback)

  buzzerStop();
  digitalWrite(RELAY_PIN, HIGH);   // Fan OFF

  if (alarmReason == "Max Occupancy!") {
    occupancy = 0;
    irCount   = 0;
    Serial.println("Occupancy reset to 0");
  }

  currentAlarm = ALARM_NONE;

  Serial.println("✓ Acknowledged by: " + code + " | Was: " + alarmReason);
  lcdLine0 = "";
  lcdLine1 = "";
  lcdWrite("Acknowledged!", "Code: " + code);
  lcdMsgActive = true;
  lcdMsgUntil  = millis() + 2000;
}

void checkGasAlarms() {
  if (alarmActive || inCooldown) return;
  if (gasAlarmLatched) return;   // latched — must be silenced by code first
  if      (lastNH3 >= NH3_THRESHOLD) { gasAlarmLatched = true; triggerAlarm("High NH3!", ALARM_GAS); }
  else if (lastH2S >= H2S_THRESHOLD) { gasAlarmLatched = true; triggerAlarm("High H2S!", ALARM_GAS); }
}

// ════════════════════════════════════
// LCD PAGES
// ════════════════════════════════════

void showPageOcc() {
  String l0 = "Occ:" + String(occupancy) + "/20 H:" + String((int)lastHum) + "%";
  String l1;
  if (inCooldown) {
    unsigned long remaining = (COOLDOWN_MS - (millis() - cooldownStart)) / 1000;
    l1 = "Cooldown: " + String(remaining) + "s";
  } else {
    l1 = "Status: OK";
  }
  lcdWrite(l0, l1);
}

void showPageGas() {
  int nh3pct = pctOfThreshold(lastNH3, NH3_THRESHOLD, 1.0f);
  int h2spct = pctOfThreshold(lastH2S, H2S_THRESHOLD, 0.5f);
  String l0 = "NH3:" + String(nh3pct) + "% H2S:" + String(h2spct) + "%";
  String l1;
  if      (nh3pct >= 100 || h2spct >= 100) l1 = "!! GAS ALARM !!";
  else if (nh3pct >= 75  || h2spct >= 75)  l1 = "Warning: High";
  else                                       l1 = "Gas: OK";
  lcdWrite(l0, l1);
}

void updateLCD() {
  if (lcdMsgActive) {
    if (millis() < lcdMsgUntil) return;
    lcdMsgActive = false;
    lcdLine0 = "";
    lcdLine1 = "";
  }
  if (millis() - lastPageSwitch >= LCD_PAGE_MS) {
    lcdPage = (lcdPage + 1) % 2;
    lastPageSwitch = millis();
  }
  switch (lcdPage) {
    case 0: showPageOcc(); break;
    case 1: showPageGas(); break;
  }
}

// ════════════════════════════════════
// WEB SERVER
// ════════════════════════════════════

void handleRoot() {
  File f = LittleFS.open("/data.html", "r");
  if (!f) {
    server.send(404, "text/plain", "data.html not found in LittleFS");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleData() {
  StaticJsonDocument<448> doc;

  doc["ammonia"]      = round(lastNH3 * 100) / 100.0;
  doc["h2s"]          = round(lastH2S * 100) / 100.0;
  doc["ammoniaPct"] = pctOfThreshold(lastNH3, NH3_THRESHOLD, 1.0f);  // NH3 pmin=1.0
  doc["h2sPct"]     = pctOfThreshold(lastH2S, H2S_THRESHOLD, 0.5f);  // H2S pmin=0.5
  doc["humidity"]     = (int)lastHum;
  doc["temp"]         = round(lastTemp * 10) / 10.0;
  doc["moisture"]     = (int)lastHum;
  doc["occupancy"]    = occupancy;
  doc["fan"]          = alarmActive;
  doc["alarm"]        = alarmActive;
  doc["reason"]       = alarmReason;
  doc["alarmType"]    = (int)currentAlarm;   // 0=none,1=hum,2=occ,3=gas
  doc["cooldown"]     = inCooldown;
  doc["cooldownLeft"] = inCooldown
    ? (long)((COOLDOWN_MS - (millis() - cooldownStart)) / 1000)
    : 0;
  doc["lastCode"]     = lastUsedCode;
  doc["lastAck"]      = lastAckTime;
  doc["currentTime"]  = getISTTime();
  doc["baselines_ok"] = true;                        // always OK — hardcoded
  doc["base_nh3"]     = NH3_BASELINE;
  doc["base_h2s"]     = H2S_BASELINE;
  doc["trig_nh3"] = NH3_THRESHOLD;   // 5.0 ppm
  doc["trig_h2s"] = H2S_THRESHOLD;   // 3.0 ppm

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ════════════════════════════════════
// SETUP
// ════════════════════════════════════

void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN,  HIGH);  // OFF
  digitalWrite(BUZZER_PIN, HIGH);  // OFF

  pinMode(IR_PIN,  INPUT);
  pinMode(DHT_PIN, INPUT_PULLUP);

  dht.begin();

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed — needed only for data.html");
  } else {
    Serial.println("LittleFS mounted OK");
  }

  // ── Gas calibration is hardcoded — print for confirmation ──
  Serial.println("[CAL] Gas baselines HARDCODED:");
  Serial.printf("[CAL] NH3 base:%.3fV  trig:%.3fV  alarm:%.1fppm\n",
                NH3_BASELINE, NH3_TRIGGER, NH3_THRESHOLD);
  Serial.printf("[CAL] H2S base:%.3fV  trig:%.3fV  alarm:%.1fppm\n",
                H2S_BASELINE, H2S_TRIGGER, H2S_THRESHOLD);

  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("Dashboard: http://");
    Serial.println(WiFi.localIP());

    // Sync NTP → IST
    configTime(UTC_OFFSET, DST_OFFSET, NTP_SERVER);
    Serial.print("Syncing NTP time");
    struct tm t;
    int ntpTries = 0;
    while (!getLocalTime(&t, 500) && ntpTries < 10) {
      Serial.print(".");
      ntpTries++;
    }
    if (getLocalTime(&t, 200)) {
      char tbuf[22];
      sprintf(tbuf, "%02d:%02d:%02d %02d/%02d/%04d",
              t.tm_hour, t.tm_min, t.tm_sec,
              t.tm_mday, t.tm_mon+1, t.tm_year+1900);
      Serial.println("\nIST time: " + String(tbuf));
    } else {
      Serial.println("\nNTP sync failed — will retry in background");
    }
  } else {
    Serial.println("\nWiFi failed — running offline");
  }

  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcdWrite("Restroom Monitor",
           WiFi.status() == WL_CONNECTED
             ? WiFi.localIP().toString()
             : "  No WiFi...    ");
  delay(2000);

  // Show calibration status briefly
  lcdLine0 = ""; lcdLine1 = "";
  lcdWrite("Cal: Hardcoded", "NH3+H2S ready");
  delay(2000);
  lcd.clear();
  lcdLine0 = "";
  lcdLine1 = "";

  // Startup beep
  digitalWrite(BUZZER_PIN, LOW);
  delay(100);
  digitalWrite(BUZZER_PIN, HIGH);

  Serial.println("=== Restroom Monitor READY ===");
  Serial.println("Buzzer: GAS=continuous | OCC=2s | HUM=5s");
  Serial.println("Gas alarm: LATCHED — requires keypad code to silence");
  Serial.printf("NH3 base:%.3fV  trig:%.3fV  alarm:%.1fppm\n",
                NH3_BASELINE, NH3_TRIGGER, NH3_THRESHOLD);
  Serial.printf("H2S base:%.3fV  trig:%.3fV  alarm:%.1fppm\n",
                H2S_BASELINE, H2S_TRIGGER, H2S_THRESHOLD);
  Serial.println("Humidity alarm: 70%");
  Serial.println("Max occupancy : 20");
  Serial.println("Cooldown      : 5 min");
  Serial.println("==============================");
}

// ════════════════════════════════════
// LOOP
// ════════════════════════════════════

void loop() {

  // ── Web server ──
  server.handleClient();

  // ── Non-blocking buzzer driver ──
  buzzerTick();

  // ── WiFi watchdog ──
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck >= 10000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost — reconnecting...");
      WiFi.reconnect();
    }
  }

  // ── Cooldown timer ──
  if (inCooldown && (millis() - cooldownStart >= COOLDOWN_MS)) {
    inCooldown      = false;
    gasAlarmLatched = false;   // reset latch after cooldown so next event fires fresh
    Serial.println("Cooldown over — monitoring resumed");
  }

 // ── IR occupancy with debounce ──
bool currentIR = digitalRead(IR_PIN);
if (currentIR == LOW && lastIRState == HIGH &&
    (millis() - lastIRTime >= 1000)) {
  irCount++;
  // Every 2 triggers = 1 person (enter + exit)
  // But show current people inside = entries - exits
  // irCount odd = someone just entered, even = they left
  occupancy = (irCount + 1) / 2;  // rounds up so entry counts immediately
  lastIRTime = millis();
  Serial.printf("IR trigger #%d | Occupancy: %d\n", irCount, occupancy);
}
lastIRState = currentIR;


  // ── DHT22 every 2s ──
  static unsigned long lastDHT = 0;
  if (millis() - lastDHT >= 2000) {
    lastDHT = millis();
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      lastHum  = h;
      lastTemp = t;
      Serial.printf("Hum: %.1f%% | Temp: %.1f°C | Occ: %d\n", h, t, occupancy);
      if (!alarmActive && !inCooldown) {
        if      (occupancy >= MAX_OCCUPANCY) triggerAlarm("Max Occupancy!", ALARM_OCCUPANCY);
        else if (h > HUM_THRESHOLD)         triggerAlarm("High Humidity!", ALARM_HUMIDITY);
      }
    } else {
      Serial.println("DHT22 read failed");
    }
  }

  // ── Gas sensors every 3s ──
  static unsigned long lastGAS = 0;
  if (millis() - lastGAS >= 3000) {
    lastGAS = millis();
    float nh3v = readVolts(NH3_PIN);
    float h2sv = readVolts(H2S_PIN);
    lastNH3 = toPPM(nh3v, NH3_BASELINE, NH3_RL, NH3_SLOPE, 1.0f,  300.0f);
    lastH2S = toPPM(h2sv, H2S_BASELINE, H2S_RL, H2S_SLOPE, 0.5f,   50.0f);
    Serial.printf("NH3: %.4fV → %.2fppm (%d%%) | H2S: %.4fV → %.2fppm (%d%%)\n",
              nh3v, lastNH3, pctOfThreshold(lastNH3, NH3_THRESHOLD, 1.0f),
              h2sv, lastH2S, pctOfThreshold(lastH2S, H2S_THRESHOLD, 0.5f));
    checkGasAlarms();
  }

  // ── LCD ──
  if (!alarmActive) updateLCD();

  // ── Keypad ──
  char key = keypad.getKey();
  if (key) {
    Serial.print("Key pressed: [");
    Serial.print(key);
    Serial.println("]");

    if (key == '#') {
      if (alarmActive) {
        if (isValidCode(enteredCode)) {
          silenceAlarm(enteredCode);
        } else {
          Serial.println("✗ Invalid: " + enteredCode);
          lcdLine1 = "";
          lcdWrite("Enter Code:", "Invalid Code!");
          lcdMsgActive = true;
          lcdMsgUntil  = millis() + 1500;
          // 3 short rejection beeps (blocking is fine here — brief)
          buzzerStop();
          for (int i = 0; i < 3; i++) {
            digitalWrite(BUZZER_PIN, LOW);  delay(80);
            digitalWrite(BUZZER_PIN, HIGH); delay(80);
          }
          // Resume the alarm pattern
          buzzerStart();
        }
      }
      enteredCode = "";

    } else if (key == '*') {
  enteredCode = "";
  if (alarmActive) {
    lcdLine1 = "";
    lcdWrite("Enter Code:", "____");
  }

  // reset cooldown
  if (inCooldown) {
    inCooldown      = false;
    gasAlarmLatched = false;
    Serial.println("Cooldown reset by * key");
    lcdLine0 = ""; lcdLine1 = "";
    lcdWrite("Cooldown Reset", "Monitoring OK");
    lcdMsgActive = true;
    lcdMsgUntil  = millis() + 2000;
  }

  Serial.println("Code cleared");
}
     else {
      if (enteredCode.length() < 4) {
        enteredCode += key;
        if (alarmActive) {
          String display = "";
          for (int i = 0; i < (int)enteredCode.length(); i++) display += "*";
          for (int i = enteredCode.length(); i < 4; i++) display += "_";
          lcdLine1 = "";
          lcdWrite("Enter Code:", display);
        }
      }
    }
  }
}