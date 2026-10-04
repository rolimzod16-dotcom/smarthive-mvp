# SmartHive MVP

ESP32 firmware plus a Vercel telemetry API and monitoring dashboard.

## Connected hardware

| Module | ESP32 pins |
| --- | --- |
| SHT31 | VIN→3V3, GND→GND, SCL→D22, SDA→D21 |
| ATGM336H GPS | VCC→3V3, GND→GND, TX→RX2/D16, RX→TX2/D17 |
| microSD adapter | VCC→VIN/5V, GND→GND, CS→D5, SCK→D18, MOSI→D23, MISO→D19 |
| RYLR998 LoRa | VDD→3V3, GND→GND, TXD→D27, RXD→D26, RST→D25 |
| INMP441 microphone | VDD→3V3, GND→GND, SCK→D32, WS→D33, SD→D34 |
| IR channels | OUT A→D35, OUT B→D36, common GND |
| DS18B20 probe | Red→3V3, Black→GND, Yellow/DATA→D4, 4.7kΩ DATA↔3V3 |
| HX711 scale | VCC→3V3, GND→GND, DT→D13, SCK→D14 |

All optional modules fail open: missing GPS, SD, LoRa, microphone, DS18B20, IR or HX711 never prevents Wi-Fi boot.
The HX711 reports raw readings immediately; kilograms remain blank until the assembled platform is calibrated.

## Scale calibration

Open the serial monitor at 115200 baud. With the empty platform send `TARE`. Put a known
weight on the platform and send `CAL 10` (replace 10 with its weight in kilograms). The tare
and calibration factor are stored in ESP32 NVS. `SCALE?` prints the current scale status.

## Phone flashing

1. Open the latest successful **Build ESP32 firmware** workflow run in GitHub Actions.
2. Download the `smarthive-full-0.4.0-factory` artifact and unzip it.
3. Flash `smarthive-full-0.4.0-factory.bin` at offset `0x0`.
4. Keep chip `ESP32`, baud `115200`, Bootloader Auto on, then press Flash.
5. On first boot connect to Wi-Fi `SmartHive-Setup` and enter home Wi-Fi, API URL, API key, and device ID.

## Vercel

Deploy the repository to Vercel and set `DEVICE_API_KEY`. For durable history add an Upstash Redis integration; the app accepts either `UPSTASH_REDIS_REST_URL` / `UPSTASH_REDIS_REST_TOKEN` or the older `KV_REST_API_URL` / `KV_REST_API_TOKEN` names.

The ESP32 API URL is:

`https://YOUR-PROJECT.vercel.app/api/telemetry`
