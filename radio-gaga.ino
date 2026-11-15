#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "config.h"

Preferences prefs;
static bool usedFastConnect = false;
static bool wifiBegun = false;
static uint8_t cachedBssid[6];
static uint8_t cachedChannel = 0;
static bool hasCached = false;

int currentZone = -1;
bool currentAm = false;
int currentVolume = -1;
bool repostZone = false;

constexpr int HTTP_FAILURE_THRESHOLD = 3;
constexpr unsigned long HTTP_BACKOFF_MS = 5000;
int httpFailures = 0;
unsigned long lastHttpAttempt = 0;

void recordHttpResult(bool ok) {
  lastHttpAttempt = millis();
  httpFailures = ok ? 0 : httpFailures + 1;
}

bool httpReady() {
  if (httpFailures < HTTP_FAILURE_THRESHOLD) return true;
  return millis() - lastHttpAttempt >= HTTP_BACKOFF_MS;
}

void logStamp() {
  unsigned long ms = millis();
  Serial.printf("[%lu.%03lu] ", ms / 1000, ms % 1000);
}

void logLine(const char* fmt, ...) {
  char buf[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  logStamp();
  Serial.println(buf);
}

bool switchIsOff() {
  return digitalRead(AM_PIN) != LOW && digitalRead(FM_PIN) != LOW;
}

void checkOff() {
  if (switchIsOff()) {
    delay(100);
    if (switchIsOff()) enterDeepSleep();
  }
}

bool postRoute(const char* route) {
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "%s%s", SPEAKER_API, route);
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT);
  int code = http.POST("");
  logLine("POST %s → %d", url, code);
  http.end();
  bool ok = code >= 200 && code < 300;
  recordHttpResult(ok);
  return ok;
}

#ifdef KEEPALIVE_ROUTE
unsigned long lastKeepalive = 0;

void sendKeepalive() {
  if (currentZone < 0 || repostZone || millis() - lastKeepalive < KEEPALIVE_INTERVAL || !httpReady()) return;
  // A failed keepalive may mean the speaker already timed playback out, so have loop() re-post the zone.
  // currentZone is left untouched: resetting it would drop resolveZone()'s hysteresis and could flip playlists.
  if (postRoute(KEEPALIVE_ROUTE)) lastKeepalive = millis();
  else repostZone = true;
  checkOff();
}
#endif

int readAdc(int pin) {
  long total = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) total += analogRead(pin);
  return total / ADC_SAMPLES;
}

int mapVolume(int raw) {
  return constrain(map(raw, 0, 4095, VOLUME_MIN, VOLUME_MAX), VOLUME_MIN, VOLUME_MAX);
}

bool sendVolume(int level, int raw) {
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "%s%s", SPEAKER_API, VOLUME_ROUTE);
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT);
  http.addHeader("Content-Type", "application/json");
  char body[32];
  snprintf(body, sizeof(body), "{\"volume\":%d}", level);
  int code = http.POST(body);
  logLine("Volume → %d%% raw=%d (%d)", level, raw, code);
  http.end();
  bool ok = code >= 200 && code < 300;
  recordHttpResult(ok);
  return ok;
}

constexpr int zoneBoundary(int i) { return (i + 1) * 4095 / NUM_ZONES; }

int resolveZone(int tuning) {
  int zone = 0;
  for (int i = 0; i < NUM_ZONES - 1; i++) {
    int boundary = zoneBoundary(i);
    int offset;
    if (currentZone < 0) offset = 0;
    else if (currentZone <= i) offset = HYSTERESIS;
    else offset = -HYSTERESIS;
    if (tuning >= boundary + offset) zone = i + 1;
    else break;
  }
  return zone;
}

void loadCachedAp() {
  prefs.begin("wifi", true);
  size_t n = prefs.getBytes("bssid", cachedBssid, 6);
  cachedChannel = prefs.getUChar("channel", 0);
  prefs.end();
  hasCached = (n == 6 && cachedChannel > 0);
}

void saveCachedAp() {
  uint8_t* bssid = WiFi.BSSID();
  int32_t channel = WiFi.channel();
  if (!bssid || channel <= 0) return;
  if (hasCached && cachedChannel == channel && memcmp(cachedBssid, bssid, 6) == 0) return;
  prefs.begin("wifi", false);
  prefs.putBytes("bssid", bssid, 6);
  prefs.putUChar("channel", (uint8_t)channel);
  prefs.end();
  memcpy(cachedBssid, bssid, 6);
  cachedChannel = channel;
  hasCached = true;
  logLine("Saved BSSID+channel to NVS");
}

void clearCachedAp() {
  prefs.begin("wifi", false);
  prefs.remove("bssid");
  prefs.remove("channel");
  prefs.end();
  hasCached = false;
}

void beginWiFi() {
  IPAddress ip(STATIC_IP);
  IPAddress gw(STATIC_GATEWAY);
  IPAddress sn(STATIC_SUBNET);
  IPAddress dns(STATIC_DNS);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_MIN_MODEM);
  WiFi.config(ip, gw, sn, dns);
  loadCachedAp();
  if (hasCached) {
    usedFastConnect = true;
    WiFi.begin(WIFI_SSID, WIFI_PASS, cachedChannel, cachedBssid);
  } else {
    usedFastConnect = false;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  wifiBegun = true;
}

bool waitForWiFi(int maxMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < (unsigned long)maxMs) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(100);
    checkOff();
    Serial.print(".");
  }
  return WiFi.status() == WL_CONNECTED;
}

void connectWiFi() {
  unsigned long t0 = millis();
  logStamp();
  Serial.print("Connecting to WiFi");
  if (WiFi.status() != WL_CONNECTED && !wifiBegun) {
    beginWiFi();
  }
  int fastTimeout = usedFastConnect ? 2500 : 4000;
  bool ok = waitForWiFi(fastTimeout);
  if (!ok && usedFastConnect) {
    Serial.println();
    logLine("Fast connect failed, falling back to full scan");
    clearCachedAp();
    WiFi.disconnect(true, true);
    delay(100);
    usedFastConnect = false;
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiBegun = true;
    ok = waitForWiFi(6000);
  }
  Serial.println();
  if (ok) {
    logLine("Connected in %lums: %s ch=%d", millis() - t0, WiFi.localIP().toString().c_str(), WiFi.channel());
    saveCachedAp();
  } else {
    logLine("WiFi connection failed");
  }
}

void reconnectWiFi() {
  logStamp();
  Serial.print("Reconnecting to WiFi");
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (waitForWiFi(6000)) {
    Serial.println();
    logLine("Reconnected: %s", WiFi.localIP().toString().c_str());
    saveCachedAp();
  } else {
    Serial.println();
    logLine("WiFi reconnect failed");
  }
}

void enterDeepSleep() {
  digitalWrite(LED_PIN, LOW);
  if (WiFi.status() == WL_CONNECTED) {
    postRoute(STOP_ROUTE);
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  logLine("Switch OFF — entering deep sleep");
  Serial.flush();

  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en((gpio_num_t)AM_PIN);
  rtc_gpio_pullup_en((gpio_num_t)FM_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)AM_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)FM_PIN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)AM_PIN, 0);
  esp_sleep_enable_ext1_wakeup(1ULL << FM_PIN, ESP_EXT1_WAKEUP_ALL_LOW);

  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  pinMode(AM_PIN, INPUT_PULLUP);
  pinMode(FM_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  bool amActive = digitalRead(AM_PIN) == LOW;
  bool fmActive = digitalRead(FM_PIN) == LOW;
  if (!amActive && !fmActive) {
    enterDeepSleep();
  }

  beginWiFi();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1) {
    logLine("Woke from deep sleep");
  }

  connectWiFi();
  digitalWrite(LED_PIN, WiFi.status() == WL_CONNECTED ? HIGH : LOW);

  amActive = digitalRead(AM_PIN) == LOW;
  fmActive = digitalRead(FM_PIN) == LOW;
  if (!amActive && !fmActive) {
    enterDeepSleep();
  }

  currentAm = amActive;
  int tuning = readAdc(TUNING_POT_PIN);
  int zone = resolveZone(tuning);
  if (WiFi.status() == WL_CONNECTED) {
    const char** zones = currentAm ? amZones : fmZones;
    if (postRoute(zones[zone])) {
      currentZone = zone;
    }
    int rawVolume = readAdc(VOLUME_POT_PIN);
    int volume = mapVolume(rawVolume);
    if (sendVolume(volume, rawVolume)) {
      currentVolume = volume;
    }
  }

  logLine("Radio started");
}

void loop() {
  int tuning = readAdc(TUNING_POT_PIN);

  bool amActive = digitalRead(AM_PIN) == LOW;
  bool fmActive = digitalRead(FM_PIN) == LOW;

  if (!amActive && !fmActive) {
    delay(100);
    amActive = digitalRead(AM_PIN) == LOW;
    fmActive = digitalRead(FM_PIN) == LOW;
    if (!amActive && !fmActive) enterDeepSleep();
  }

  if (WiFi.status() != WL_CONNECTED) {
    reconnectWiFi();
    digitalWrite(LED_PIN, WiFi.status() == WL_CONNECTED ? HIGH : LOW);
    checkOff();
    if (WiFi.status() == WL_CONNECTED) {
      currentZone = -1;
      currentVolume = -1;
    }
  }

  bool modeChanged = amActive != currentAm;
  if (modeChanged) {
    delay(250);
    amActive = digitalRead(AM_PIN) == LOW;
    fmActive = digitalRead(FM_PIN) == LOW;
    if (!amActive && !fmActive) {
      enterDeepSleep();
    }
    modeChanged = amActive != currentAm;
    tuning = readAdc(TUNING_POT_PIN);
  }

  const char** zones = amActive ? amZones : fmZones;
  const char* mode = amActive ? "AM" : "FM";

  int zone = resolveZone(tuning);
  if (WiFi.status() == WL_CONNECTED) {
    if ((zone != currentZone || modeChanged || repostZone) && httpReady()) {
      logLine("Zone changed: %d (%s) raw=%d", zone, mode, tuning);
      digitalWrite(LED_PIN, LOW);
      if (postRoute(zones[zone])) {
        currentZone = zone;
        currentAm = amActive;
        repostZone = false;
      }
      checkOff();
      digitalWrite(LED_PIN, HIGH);
    }

#ifdef LOG_RAW_POTS
    logLine("tuning=%d volume=%d", tuning, analogRead(VOLUME_POT_PIN));
#endif

    int rawVolume = readAdc(VOLUME_POT_PIN);
    int volume = mapVolume(rawVolume);
    if (abs(volume - currentVolume) > 1 && httpReady()) {
      if (sendVolume(volume, rawVolume)) {
        currentVolume = volume;
      }
      checkOff();
    }

#ifdef KEEPALIVE_ROUTE
    sendKeepalive();
#endif
  }

  delay(50);
}
