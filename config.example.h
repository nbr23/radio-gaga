#ifndef CONFIG_H
#define CONFIG_H

// WiFi credentials — fill in before flashing
#define WIFI_SSID "" // FIXME
#define WIFI_PASS "" // FIXME

// Base URL of the speaker HTTP server (host:port)
#define SPEAKER_API "http://192.168..:8080" // FIXME incomplete IP

// Static network config, comma-separated octets
#define STATIC_IP      192, 168, ,    // FIXME missing octets
#define STATIC_GATEWAY 192, 168, , 1  // FIXME missing octet
#define STATIC_SUBNET  255, 255, 255, 0
#define STATIC_DNS     192, 168, , 1  // FIXME missing octet

// GPIO pin assignments
#define TUNING_POT_PIN 34
#define VOLUME_POT_PIN 35
#define AM_PIN 32
#define FM_PIN 33
#define LED_PIN 27

// Tuning behavior and HTTP routes
#define NUM_ZONES 7
#define HYSTERESIS 50
#define STOP_ROUTE "/stop"
#define VOLUME_ROUTE "/volume"
#define HTTP_TIMEOUT 2000
#define ADC_SAMPLES 8
// #define LOG_RAW_POTS

// Volume range sent to the speaker
#define VOLUME_MIN 10
#define VOLUME_MAX 45

const char* amZones[NUM_ZONES] = {
  "/play/radio/1",
  "/play/radio/2",
  "/play/radio/3",
  "/play/radio/4",
  "/play/radio/5",
  "/play/radio/6",
  "/play/radio/7",
};

const char* fmZones[NUM_ZONES] = {
  "/play/radio/11",
  "/play/radio/12",
  "/play/radio/13",
  "/play/radio/14",
  "/play/radio/15",
  "/play/radio/16",
  "/play/radio/17",
};

#endif
