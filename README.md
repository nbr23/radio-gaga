# radio-gaga

ESP32 firmware that turns a gutted Panasonic RF-2400 radio into a remote for a
networked speaker: the tuning dial picks a playlist, the volume dial sets the
volume, and the OFF/AM/FM slider switches between two playlist banks or puts
the ESP32 into deep sleep.

Writeup: [Turning a cheap Panasonic radio into a toddler jukebox controller](https://nbr23.com/posts/turning-a-cheap-panasonic-radio-into-a-toddler-jukebox-controller/)

## Wiring

ESP32-WROOM-32 DevKit V1, powered by 4x AA NiMH on VIN/GND.

| GPIO | Function   |                           |
|------|------------|---------------------------|
| 34   | Tuning pot | 3.3V / GPIO / GND         |
| 35   | Volume pot | 3.3V / GPIO / GND         |
| 32   | AM sense   | LOW = AM, wakes from sleep |
| 33   | FM sense   | LOW = FM, wakes from sleep |
| 27   | LED        | HIGH = on                 |

Both sense pins HIGH means OFF.

## Speaker API

All requests are POSTs to `SPEAKER_API`:

| Route                | Purpose                                                    |
|----------------------|------------------------------------------------------------|
| `/play/radio/<name>` | Start a playlist                                           |
| `/stop`              | Stop playback, sent before deep sleep                      |
| `/volume`            | Set volume, body `{"volume": N}`                           |
| `/keepalive/radio`   | Optional, sent periodically if `KEEPALIVE_ROUTE` is defined |

## Setup

1. `cp config.example.h config.h`
2. Fill in the `FIXME` values (WiFi, speaker URL, static IP) and edit
   `amZones[]` / `fmZones[]` to match your playlists.
3. Flash `radio-gaga.ino` to an ESP32 Dev Module.

Uncomment `LOG_RAW_POTS` in `config.h` to print raw ADC readings at 115200 baud.
