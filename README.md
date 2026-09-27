# PAWMAN — the secret life of pets

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![Python 3.10+](https://img.shields.io/badge/python-3.10%2B-blue.svg)
![Hardware: ESP32](https://img.shields.io/badge/hardware-ESP32--CAM%20%7C%20ESP32--C6-orange.svg)

A DIY smart collar that finds out what your dog does when you're not home. You get a WhatsApp alert when the dog gets into trouble, and at the end of the day it writes an illustrated comic episode of **The Pawman Show** about the dog's day.

## Description

Pawman has two small boards that go on a dog's harness, plus a Python server that runs on a laptop:

- **Collar camera (ESP32-CAM).** Takes a JPEG every 1.5–8 seconds. It shoots faster when the dog is active.
- **Motion tag (ESP32-C6 + MPU6050).** Sends accelerometer and gyro features every second, plus button presses.
- **Server (FastAPI).** Works out the dog's activity (rest / walk / run / shake) and uses Claude vision to read the zone signs in the camera frames. When the dog enters a "risk zone" such as the sofa, the kitchen bin or the shoes, it sends a WhatsApp alert with a photo. At the end of the day it picks the best frames and writes a comic-style episode.

```
 ESP32-CAM ──JPEG every 1.5–8 s──┐
                                 ├──► laptop: server/app.py ──► WhatsApp alerts
 ESP32-C6 + MPU6050 ──1 s JSON───┘         │                      (Twilio / CallMeBot)
   (activity + button)                     ├──► live dashboard  http://<laptop>:8000
                                           └──► episode page    http://<laptop>:8000/episode
```

The two boards never talk to each other. The server matches their data by timestamp, so you can build and debug each board on its own. You can also run the whole thing **with no hardware** by using the included simulator.

**Features**
- Live dashboard with the latest frame, current activity, a timeline and an event feed
- Zone detection: Claude vision reads printed signs (`SOFA`, `KITCHEN BIN`, …) in the camera frames
- Activity thresholds that you can tune live without reflashing (`POST /api/thresholds`)
- WhatsApp alerts through Twilio (with photos) or CallMeBot (text only)
- An AI-written end-of-day episode, with a template story as a fallback when there's no API key
- Backup camera options: a phone browser (`/phonecam`) or the Android "IP Webcam" app
- A simulator for demos and testing with no hardware

## Visuals

_Add a screenshot of the dashboard and an example episode here, e.g. `docs/dashboard.png`._

## Installation

### Requirements
- Python 3.10+
- Arduino IDE with the Espressif **esp32** boards package (v3.x)
- Optional: an [Anthropic API key](https://console.anthropic.com/), a [Twilio](https://www.twilio.com/) account or a [CallMeBot](https://www.callmebot.com/) key, and [ngrok](https://ngrok.com/)

### Hardware (bill of materials)
| Part | Purpose |
|---|---|
| AI-Thinker ESP32-CAM (OV2640) | collar camera |
| CP2102 USB-serial adapter | flashing the ESP32-CAM |
| ESP32-C6 board (e.g. Glyph C6) | motion tag |
| MPU6050 IMU | accelerometer + gyro |
| Push button | "mark moment" / "end of day" |
| 3.7 V LiPo + 5 V boost/charger (e.g. 134N3P, or TP4056 + MT3608) | power |
| 470–1000 µF ≥10 V electrolytic capacitor (recommended) | stops the camera from browning out |

### 1. Network
Use **your own phone hotspot or router**. Venue and office Wi-Fi usually block devices from reaching each other.
- Create a **2.4 GHz** network (ESP32s can't use 5 GHz). The sketches expect SSID `PAWMAN`, password `pawman123` by default.
- Connect the laptop to the same network.

### 2. Server
```bash
git clone https://github.com/<your-username>/pawman.git
cd pawman/server
pip install -r requirements.txt
cp .env.example .env        # everything in it is optional
python app.py
```
At startup the server prints the address to paste into both sketches, e.g. `SERVER = "http://192.168.43.12:8000"`. If Windows asks about the firewall, allow access on **private networks**.

**Optional settings in `.env`:**
| Feature | What to set | Without it |
|---|---|---|
| Vision zone detection + AI-written episode | `ANTHROPIC_API_KEY` | template story; zones only come from the simulator |
| WhatsApp with photos | Twilio sandbox keys + `WHATSAPP_TO` + `PUBLIC_URL` (from `ngrok http 8000`) | alerts appear on the dashboard only |
| WhatsApp text only | `CALLMEBOT_PHONE` + `CALLMEBOT_KEY` | ″ |
| Phone as a backup camera | `PHONE_CAM_URL` (Android "IP Webcam" snapshot URL) | — |

See [`server/.env.example`](server/.env.example) for every setting, including dog and owner names, zones and activity thresholds.

### 3. Collar camera: ESP32-CAM ([`firmware/pawcam_esp32cam`](firmware/pawcam_esp32cam))
Arduino IDE: board **AI Thinker ESP32-CAM**. Edit the Wi-Fi and `SERVER` lines at the top of the sketch.

**Flashing with a CP2102:**
| CP2102 | ESP32-CAM |
|---|---|
| 5V | 5V |
| GND | GND |
| TXD | U0R |
| RXD | U0T |
| — | **IO0 → GND** (flash mode only) |

Press RST, then Upload. After it uploads, **remove the IO0–GND wire** and press RST. The serial monitor at 115200 baud should show `frame 23456 bytes -> next in 4000 ms`.

**Power:**
```
LiPo 3.7 V ──► 134N3P (BAT+/BAT-) ──► 5V OUT ──► ESP32-CAM 5V + GND
                                     └────────► ESP32-C6 5V/VBUS + GND   (or give the C6 its own LiPo)
```
Alternative chain: LiPo → TP4056 (OUT+/OUT−) → MT3608 (**set it to 5.0 V with a multimeter before connecting**) → 5V.

**If the camera keeps resetting (brownouts):**
- The sketch already turns off the brownout detector, lowers Wi-Fi transmit power and uses a slower camera clock.
- Always feed the camera **5V, never 3.3V**. Keep the 5V/GND wires short (5–8 cm), thick and twisted together.
- Fit a 470–1000 µF capacitor across 5V (+) and GND (−), with the stripe to GND.
- If it still resets, set `FRAMESIZE_QVGA` and `WIFI_POWER_8_5dBm` in the sketch.

### 4. Motion tag: ESP32-C6 + MPU6050 ([`firmware/pawtag_c6`](firmware/pawtag_c6))
Arduino IDE: board **ESP32C6 Dev Module**, **USB CDC On Boot: Enabled**.

| MPU6050 | ESP32-C6 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO4 |
| SCL | GPIO5 |

Wire the **button** between **GPIO3** and **GND**. The onboard BOOT button (GPIO9) also works.

## Usage

### Try it with no hardware
With the server running, open a second terminal:
```bash
cd server
python simulate.py                  # scripted "home alone" run, ends with an episode
python simulate.py --fast           # 3x speed
python simulate.py --photos myrun/  # use real photos instead of drawn ones
python simulate.py --vision         # don't declare zones; let Claude read the signs
```
Then open **http://localhost:8000** for the dashboard and **http://localhost:8000/episode** for the episode.

### With the real collar
1. **Signs:** print `SOFA`, `KITCHEN BIN`, `SHOES`, `WATER BOWL`, `FRONT DOOR` and `BED` in huge black letters on white A4 paper. Tape each one at dog-eye height, facing the direction the dog will travel. SOFA, KITCHEN BIN and SHOES are **risk zones** that trigger alerts. You can change both lists with `ZONES` and `RISK_ZONES`.
2. **Mounting:** put the camera on a **harness** (upper back or chest strap), pointing forward and slightly down, and keep the lens clear of fur. Fix the motion tag firmly anywhere on the harness, because a loose tag reads as "running". Total weight is about 70–90 g, so check that's OK for the dog.
3. **Button:** a short press marks a moment. **Holding it for 2 s** ends the day and generates the episode, which takes about 20–40 s. You can also press "End of day" on the dashboard.
4. **Timing:** `FAKE_DAY=09:00-18:00` stretches a short run over a pretend workday in the story, so a 3-minute run reads like a whole day. Set it to `""` to use the real clock.

### Tuning activity detection
The server classifies activity, so you can tune thresholds without reflashing. Walk and shake the tag while you watch the numbers on the dashboard, then run:
```bash
curl -X POST localhost:8000/api/thresholds -H "Content-Type: application/json" -d "{\"walk_std\":0.1,\"run_std\":0.5}"
```

### HTTP API
| Method | Path | Description |
|---|---|---|
| `POST` | `/frame` | JPEG body from a camera; returns ms until the next shot. Optional `X-Zone` header. |
| `POST` | `/imu` | JSON IMU features from the motion tag |
| `GET` | `/api/state` | full dashboard state |
| `POST` | `/api/thresholds` | update activity thresholds |
| `POST` | `/api/end_of_day` | generate the episode |
| `POST` | `/api/test_alert` | send a test WhatsApp alert |
| `POST` | `/api/reset` | archive `data/` to `data_archive_<ts>/` and start fresh |
| `GET` | `/`, `/episode`, `/phonecam` | dashboard, episode, phone-browser camera |

### Project layout
```
firmware/pawcam_esp32cam/   ESP32-CAM sketch
firmware/pawtag_c6/         IMU + button sketch
server/app.py               server: ingest, rules, WhatsApp, vision, episode
server/simulate.py          fake dog for testing and demos
server/templates/           dashboard, episode and phone-camera pages
```

## Support
Please open an issue on GitHub for bugs, questions or hardware trouble. Include the serial monitor output or the server log if you can.

## Roadmap
- [ ] Standalone mode with no laptop (on-device rules, or a cloud server)
- [ ] Zone detection without printed signs (e.g. BLE beacons or room recognition)
- [ ] Battery-life optimisations (deep sleep between frames)
- [ ] 3D-printable harness mount
- [ ] Multi-day history and episode archive

## Contributing
Contributions are welcome!

1. Fork the repo and create a branch: `git checkout -b my-feature`
2. Make your changes. Test them with `python simulate.py` if you have no hardware.
3. **Never commit your `.env`**, API keys or phone numbers. `.gitignore` already excludes `.env` and `server/data/`.
4. Open a pull request that describes what changed and how you tested it.

For big changes, please open an issue first so the approach can be discussed.

## Authors and acknowledgment
- **Siddharth Arya** — creator

Built with [FastAPI](https://fastapi.tiangolo.com/), [Claude](https://www.anthropic.com/claude) by Anthropic, [Twilio](https://www.twilio.com/) and [CallMeBot](https://www.callmebot.com/), on Espressif ESP32 hardware.

## License
[MIT](LICENSE)

## Project status
Started as a hackathon project and actively tinkered with. It is not production-ready: the server has no authentication, so run it only on a network you trust.
