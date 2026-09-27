#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Adafruit_SHT31.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>

// SmartHive wiring used in this build.
static constexpr int PIN_SDA = 21;
static constexpr int PIN_SCL = 22;
static constexpr int PIN_GPS_RX = 16; // GPS TX -> ESP RX2
static constexpr int PIN_GPS_TX = 17; // GPS RX -> ESP TX2
static constexpr int PIN_SD_CS = 5;
static constexpr int PIN_LORA_RX = 27; // LoRa TXD -> ESP RX
static constexpr int PIN_LORA_TX = 26; // LoRa RXD -> ESP TX
static constexpr int PIN_LORA_RST = 25;

static constexpr uint32_t SEND_INTERVAL_MS = 30000;
static constexpr uint32_t GPS_BAUD = 9600;
static constexpr uint32_t LORA_BAUD = 115200;

Adafruit_SHT31 sht31 = Adafruit_SHT31();
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial loraSerial(1);
Preferences prefs;

bool shtOk = false;
bool sdOk = false;
bool loraOk = false;
unsigned long lastSend = 0;
char apiUrl[160] = "";
char apiKey[80] = "";
char deviceId[40] = "smarthive-01";

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (char c : value) {
    if (c == '\\' || c == '\"') out += '\\';
    out += c;
  }
  return out;
}

bool testLoRa() {
  while (loraSerial.available()) loraSerial.read();
  loraSerial.print("AT\r\n");
  const unsigned long start = millis();
  String response;
  while (millis() - start < 800) {
    while (loraSerial.available()) response += char(loraSerial.read());
    if (response.indexOf("+OK") >= 0 || response.indexOf("OK") >= 0) return true;
    delay(5);
  }
  return false;
}

void appendCsv(float temperature, float humidity, double lat, double lng,
               bool gpsValid, int satellites) {
  if (!sdOk) return;
  File f = SD.open("/telemetry.csv", FILE_APPEND);
  if (!f) return;
  if (f.size() == 0) {
    f.println("uptime_ms,temperature_c,humidity_pct,latitude,longitude,gps_valid,satellites,wifi_rssi,lora_ok");
  }
  f.printf("%lu,%.2f,%.2f,%.6f,%.6f,%d,%d,%d,%d\n",
           millis(), temperature, humidity, lat, lng, gpsValid ? 1 : 0,
           satellites, WiFi.RSSI(), loraOk ? 1 : 0);
  f.close();
}

String buildPayload() {
  const float temperature = shtOk ? sht31.readTemperature() : NAN;
  const float humidity = shtOk ? sht31.readHumidity() : NAN;
  const bool gpsValid = gps.location.isValid() && gps.location.age() < 10000;
  const double lat = gpsValid ? gps.location.lat() : 0.0;
  const double lng = gpsValid ? gps.location.lng() : 0.0;
  const int satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;

  appendCsv(temperature, humidity, lat, lng, gpsValid, satellites);

  String payload = "{";
  payload += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  payload += "\"uptimeMs\":" + String(millis()) + ",";
  payload += "\"temperatureC\":" + (isnan(temperature) ? String("null") : String(temperature, 2)) + ",";
  payload += "\"humidityPct\":" + (isnan(humidity) ? String("null") : String(humidity, 2)) + ",";
  payload += "\"latitude\":" + (gpsValid ? String(lat, 6) : String("null")) + ",";
  payload += "\"longitude\":" + (gpsValid ? String(lng, 6) : String("null")) + ",";
  payload += "\"gpsValid\":" + String(gpsValid ? "true" : "false") + ",";
  payload += "\"satellites\":" + String(satellites) + ",";
  payload += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  payload += "\"sht31Ok\":" + String(shtOk ? "true" : "false") + ",";
  payload += "\"microSdOk\":" + String(sdOk ? "true" : "false") + ",";
  payload += "\"loraOk\":" + String(loraOk ? "true" : "false") + ",";
  payload += "\"firmware\":\"0.1.0\"";
  payload += "}";
  return payload;
}

void sendTelemetry() {
  loraOk = testLoRa();
  const String payload = buildPayload();
  Serial.println(payload);

  if (WiFi.status() != WL_CONNECTED || strlen(apiUrl) == 0) return;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(apiUrl)) return;
  http.addHeader("Content-Type", "application/json");
  if (strlen(apiKey)) http.addHeader("x-api-key", apiKey);
  const int code = http.POST(payload);
  Serial.printf("Telemetry HTTP status: %d\n", code);
  http.end();
}

void setupWifiPortal() {
  prefs.begin("smarthive", false);
  String storedUrl = prefs.getString("apiUrl", "");
  String storedKey = prefs.getString("apiKey", "");
  String storedDevice = prefs.getString("deviceId", "smarthive-01");
  storedUrl.toCharArray(apiUrl, sizeof(apiUrl));
  storedKey.toCharArray(apiKey, sizeof(apiKey));
  storedDevice.toCharArray(deviceId, sizeof(deviceId));

  WiFiManager wm;
  WiFiManagerParameter pUrl("api", "Vercel API URL", apiUrl, sizeof(apiUrl));
  WiFiManagerParameter pKey("key", "Device API key", apiKey, sizeof(apiKey));
  WiFiManagerParameter pDevice("device", "Device ID", deviceId, sizeof(deviceId));
  wm.addParameter(&pUrl);
  wm.addParameter(&pKey);
  wm.addParameter(&pDevice);
  wm.setConfigPortalTimeout(240);

  if (!wm.autoConnect("SmartHive-Setup")) {
    Serial.println("Wi-Fi setup timed out; continuing offline.");
    return;
  }

  strlcpy(apiUrl, pUrl.getValue(), sizeof(apiUrl));
  strlcpy(apiKey, pKey.getValue(), sizeof(apiKey));
  strlcpy(deviceId, pDevice.getValue(), sizeof(deviceId));
  prefs.putString("apiUrl", apiUrl);
  prefs.putString("apiKey", apiKey);
  prefs.putString("deviceId", deviceId);
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("SmartHive booting...");

  Wire.begin(PIN_SDA, PIN_SCL);
  shtOk = sht31.begin(0x44);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, HIGH);
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS, SPI);

  setupWifiPortal();
  sendTelemetry();
  lastSend = millis();
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendTelemetry();
  }
  delay(2);
}

