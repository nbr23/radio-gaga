#ifndef CONFIG_H
#define CONFIG_H

// Copy this file to config.h and fill in your own values.
// config.h is gitignored so your credentials never get committed.

// --- WiFi ---
#define WIFI_SSID "YOUR_SSID"      // FIXME
#define WIFI_PASS "YOUR_PASSWORD"  // FIXME

// --- Speaker ---
// Base URL (host:port) of the speaker's HTTP API. No trailing slash.
// Routes below are appended to this.
#define SPEAKER_API "http://192.168.1.42:8080"  // FIXME speaker IP:port

// --- Static network config ---
// Comma-separated octets (expanded into IPAddress(...) in the sketch).
#define STATIC_IP      192, 168, 1, 23  // FIXME this device's IP
#define STATIC_GATEWAY 192, 168, 1, 1   // FIXME router IP
#define STATIC_SUBNET  255, 255, 255, 0
#define STATIC_DNS     192, 168, 1, 1   // FIXME usually same as gateway

// --- GPIO pin assignments ---
// 34/35 are ADC1 input-only pins (work with WiFi active). 32/33 read the
// 3-position OFF/AM/FM switch and double as deep-sleep wake sources.
// 27 drives the LED.
#define TUNING_POT_PIN 34  // ADC, 0-4095, selects zone/playlist
#define VOLUME_POT_PIN 35  // ADC, 0-4095, mapped to VOLUME_MIN..MAX
#define AM_PIN 32          // LOW = switch in AM position
#define FM_PIN 33          // LOW = switch in FM position
#define LED_PIN 27         // HIGH = on. OFF = both AM/FM HIGH -> deep sleep

// --- Tuning behavior ---
// The tuning dial's ADC range is split into NUM_ZONES equal bands; each
// band maps to one entry in amZones[]/fmZones[] below. HYSTERESIS (in ADC
// counts) is the dead-band around each boundary that stops jitter from
// flipping zones back and forth. ADC_SAMPLES readings are averaged per poll.
#define NUM_ZONES 7
#define HYSTERESIS 50
#define ADC_SAMPLES 8
// #define LOG_RAW_POTS   // uncomment to print raw tuning/volume ADC each loop

// --- HTTP ---
#define STOP_ROUTE "/stop"      // POST: stop playback (sent before deep sleep)
#define VOLUME_ROUTE "/volume"  // POST {"volume":N}
#define HTTP_TIMEOUT 2000        // ms per request

// --- Volume range sent to the speaker ---
// The dial's full travel maps onto this range (the speaker's own volume scale).
#define VOLUME_MIN 10
#define VOLUME_MAX 45

// --- Zone -> playlist mappings ---
// One route per zone, low ADC (dial fully one way) first. The 3-position
// switch picks which array is live: AM position uses amZones, FM uses fmZones.
// This gives two independent banks of NUM_ZONES playlists on the one dial.
// Edit the routes to match the playlists configured on your speaker.
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
