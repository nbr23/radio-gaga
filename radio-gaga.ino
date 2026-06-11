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
  Serial.print("POST ");
  Serial.print(url);
  Serial.print(" → ");
  Serial.println(code);
  http.end();
  return code >= 200 && code < 300;
}

int readAdc(int pin) {
  long total = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) total += analogRead(pin);
  return total / ADC_SAMPLES;
}

int readVolume() {
  return constrain(map(readAdc(VOLUME_POT_PIN), 0, 4095, VOLUME_MIN, VOLUME_MAX), VOLUME_MIN, VOLUME_MAX);
}

bool sendVolume(int level) {
  HTTPClient http;
  char url[128];
  snprintf(url, sizeof(url), "%s%s", SPEAKER_API, VOLUME_ROUTE);
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT);
  http.addHeader("Content-Type", "application/json");
  char body[32];
  snprintf(body, sizeof(body), "{\"volume\":%d}", level);
  int code = http.POST(body);
  Serial.print("Volume → ");
  Serial.print(level);
  Serial.print("% (");
  Serial.print(code);
  Serial.println(")");
  http.end();
  return code >= 200 && code < 300;
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
  Serial.println("Saved BSSID+channel to NVS");
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
  Serial.print("Connecting to WiFi");
  if (WiFi.status() != WL_CONNECTED && !wifiBegun) {
    beginWiFi();
  }
  int fastTimeout = usedFastConnect ? 2500 : 4000;
  bool ok = waitForWiFi(fastTimeout);
  if (!ok && usedFastConnect) {
    Serial.println();
    Serial.println("Fast connect failed, falling back to full scan");
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
    Serial.print("Connected in ");
    Serial.print(millis() - t0);
    Serial.print("ms: ");
    Serial.print(WiFi.localIP());
    Serial.print(" ch=");
    Serial.println(WiFi.channel());
    saveCachedAp();
  } else {
    Serial.println("WiFi connection failed");
  }
}

void reconnectWiFi() {
  Serial.print("Reconnecting to WiFi");
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  if (waitForWiFi(6000)) {
    Serial.println();
    Serial.print("Reconnected: ");
    Serial.println(WiFi.localIP());
    saveCachedAp();
  } else {
    Serial.println();
    Serial.println("WiFi reconnect failed");
  }
}

void enterDeepSleep() {
  digitalWrite(LED_PIN, LOW);
  if (WiFi.status() == WL_CONNECTED) {
    postRoute(STOP_ROUTE);
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Switch OFF — entering deep sleep");
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
    Serial.println("Woke from deep sleep");
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
    int volume = readVolume();
    if (sendVolume(volume)) {
      currentVolume = volume;
    }
  }

  Serial.println("Radio started");
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
    if (zone != currentZone || modeChanged) {
      Serial.print("Zone changed: ");
      Serial.print(zone);
      Serial.print(" (");
      Serial.print(mode);
      Serial.println(")");
      digitalWrite(LED_PIN, LOW);
      if (postRoute(zones[zone])) {
        currentZone = zone;
        currentAm = amActive;
      }
      checkOff();
      digitalWrite(LED_PIN, HIGH);
    }

#ifdef LOG_RAW_POTS
    Serial.print("tuning=");
    Serial.print(tuning);
    Serial.print(" volume=");
    Serial.println(analogRead(VOLUME_POT_PIN));
#endif

    int volume = readVolume();
    if (abs(volume - currentVolume) > 1) {
      if (sendVolume(volume)) {
        currentVolume = volume;
      }
      checkOff();
    }
  }

  delay(50);
}
