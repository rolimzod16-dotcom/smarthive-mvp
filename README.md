# SmartHive MVP

ESP32 firmware plus a Vercel telemetry API and monitoring dashboard.

## Connected hardware

| Module | ESP32 pins |
| --- | --- |
| SHT31 | VIN→3V3, GND→GND, SCL→D22, SDA→D21 |
| ATGM336H GPS | VCC→3V3, GND→GND, TX→RX2/D16, RX→TX2/D17 |
| microSD adapter | VCC→VIN/5V, GND→GND, CS→D5, SCK→D18, MOSI→D23, MISO→D19 |
| RYLR998 LoRa | VDD→3V3, GND→GND, TXD→D27, RXD→D26, RST→D25 |

The INMP441 microphone is intentionally disabled until its solder bridges are repaired.

## Phone flashing

1. Open the latest successful **Build ESP32 firmware** workflow run in GitHub Actions.
2. Download the `smarthive-complete-bin` artifact and unzip it on Android.
3. In ESP32 Flash/Erase select `smarthive-complete.bin` with offset `0x0`.
4. Keep chip `ESP32`, baud `115200`, Bootloader Auto on, then press Flash.
5. On first boot connect to Wi-Fi `SmartHive-Setup` and enter home Wi-Fi, API URL, API key, and device ID.

## Vercel

Deploy the repository to Vercel and set `DEVICE_API_KEY`. For durable history add an Upstash Redis integration; the app accepts either `UPSTASH_REDIS_REST_URL` / `UPSTASH_REDIS_REST_TOKEN` or the older `KV_REST_API_URL` / `KV_REST_API_TOKEN` names.

The ESP32 API URL is:

`https://YOUR-PROJECT.vercel.app/api/telemetry`

