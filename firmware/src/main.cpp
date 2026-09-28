#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Adafruit_SHT31.h>
#include <TinyGPSPlus.h>
#include <SPI.h>
#include <SD.h>
#include <driver/i2s.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define SMARTHIVE_HAS_PRIVATE_WIFI 1
#else
#define SMARTHIVE_HAS_PRIVATE_WIFI 0
#endif

static constexpr int PIN_SDA = 21;
static constexpr int PIN_SCL = 22;
static constexpr int PIN_GPS_RX = 16;
static constexpr int PIN_GPS_TX = 17;
static constexpr int PIN_SD_CS = 5;
static constexpr int PIN_LORA_RX = 27;
static constexpr int PIN_LORA_TX = 26;
static constexpr int PIN_LORA_RST = 25;
static constexpr int PIN_MIC_SCK = 32;
static constexpr int PIN_MIC_WS = 33;
static constexpr int PIN_MIC_SD = 34;
static constexpr int PIN_IR_A = 35;
static constexpr int PIN_IR_B = 36;

static constexpr uint32_t SEND_INTERVAL_MS = 30000;
static constexpr uint32_t WIFI_RETRY_MS = 10000;
static constexpr i2s_port_t MIC_I2S_PORT = I2S_NUM_0;

const char *DEFAULT_API_URL = "https://smarthive-mvp.vercel.app/api/telemetry";

Adafruit_SHT31 sht31;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial loraSerial(1);
Preferences prefs;

bool shtOk = false;
bool sdOk = false;
bool loraOk = false;
bool micOk = false;
unsigned long lastSend = 0;
unsigned long lastWifiRetry = 0;
char apiUrl[160] = "https://smarthive-mvp.vercel.app/api/telemetry";
char apiKey[80] = "";
char deviceId[40] = "smarthive-01";

volatile uint32_t irBeamACount = 0;
volatile uint32_t irBeamBCount = 0;
volatile uint32_t lastIrAUs = 0;
volatile uint32_t lastIrBUs = 0;

void IRAM_ATTR onIrBeamA() {
  const uint32_t now = micros();
  if (now - lastIrAUs > 50000) {
    irBeamACount++;
    lastIrAUs = now;
  }
}

void IRAM_ATTR onIrBeamB() {
  const uint32_t now = micros();
  if (now - lastIrBUs > 50000) {
    irBeamBCount++;
    lastIrBUs = now;
  }
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (char c : value) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

bool setupMicrophone() {
  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = 16000,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = 0,
  };
  const i2s_pin_config_t pins = {
      .bck_io_num = PIN_MIC_SCK,
      .ws_io_num = PIN_MIC_WS,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = PIN_MIC_SD,
  };
  if (i2s_driver_install(MIC_I2S_PORT, &config, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) {
    i2s_driver_uninstall(MIC_I2S_PORT);
    return false;
  }
  i2s_zero_dma_buffer(MIC_I2S_PORT);
  return true;
}

float readSoundDbfs() {
  if (!micOk) return NAN;
  int32_t samples[256];
  size_t bytesRead = 0;
  if (i2s_read(MIC_I2S_PORT, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(250)) != ESP_OK || bytesRead == 0) return NAN;
  const size_t count = bytesRead / sizeof(samples[0]);
  double sumSquares = 0.0;
  for (size_t i = 0; i < count; i++) {
    const double normalized = static_cast<double>(samples[i]) / 2147483648.0;
    sumSquares += normalized * normalized;
  }
  const double rms = sqrt(sumSquares / count);
  if (rms < 0.000001) return -90.0f;
  return constrain(static_cast<float>(20.0 * log10(rms)), -90.0f, 0.0f);
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

void appendCsv(float temperature, float humidity, double lat, double lng, bool gpsValid,
               int satellites, float soundDbfs, uint32_t beamA, uint32_t beamB) {
  if (!sdOk) return;
  File f = SD.open("/telemetry.csv", FILE_APPEND);
  if (!f) return;
  if (f.size() == 0) {
    f.println("uptime_ms,temperature_c,humidity_pct,latitude,longitude,gps_valid,satellites,wifi_rssi,lora_ok,mic_ok,sound_dbfs,ir_a_count,ir_b_count");
  }
  f.printf("%lu,%.2f,%.2f,%.6f,%.6f,%d,%d,%d,%d,%d,%.2f,%lu,%lu\n",
           millis(), temperature, humidity, lat, lng, gpsValid ? 1 : 0, satellites,
           WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127, loraOk ? 1 : 0,
           micOk ? 1 : 0, soundDbfs, static_cast<unsigned long>(beamA), static_cast<unsigned long>(beamB));
  f.close();
}

String buildPayload() {
  const float temperature = shtOk ? sht31.readTemperature() : NAN;
  const float humidity = shtOk ? sht31.readHumidity() : NAN;
  const bool gpsValid = gps.location.isValid() && gps.location.age() < 10000;
  const double lat = gpsValid ? gps.location.lat() : 0.0;
  const double lng = gpsValid ? gps.location.lng() : 0.0;
  const int satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  const float soundDbfs = readSoundDbfs();
  uint32_t beamA;
  uint32_t beamB;
  noInterrupts();
  beamA = irBeamACount;
  beamB = irBeamBCount;
  interrupts();

  appendCsv(temperature, humidity, lat, lng, gpsValid, satellites, soundDbfs, beamA, beamB);

  String payload = "{";
  payload += "\"deviceId\":\"" + jsonEscape(deviceId) + "\",";
  payload += "\"uptimeMs\":" + String(millis()) + ",";
  payload += "\"temperatureC\":" + (isnan(temperature) ? String("null") : String(temperature, 2)) + ",";
  payload += "\"humidityPct\":" + (isnan(humidity) ? String("null") : String(humidity, 2)) + ",";
  payload += "\"latitude\":" + (gpsValid ? String(lat, 6) : String("null")) + ",";
  payload += "\"longitude\":" + (gpsValid ? String(lng, 6) : String("null")) + ",";
  payload += "\"gpsValid\":" + String(gpsValid ? "true" : "false") + ",";
  payload += "\"satellites\":" + String(satellites) + ",";
  payload += "\"wifiRssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127) + ",";
  payload += "\"sht31Ok\":" + String(shtOk ? "true" : "false") + ",";
  payload += "\"microSdOk\":" + String(sdOk ? "true" : "false") + ",";
  payload += "\"loraOk\":" + String(loraOk ? "true" : "false") + ",";
  payload += "\"microphoneOk\":" + String(micOk ? "true" : "false") + ",";
  payload += "\"soundLevelDbfs\":" + (isnan(soundDbfs) ? String("null") : String(soundDbfs, 1)) + ",";
  payload += "\"irBeamACount\":" + String(beamA) + ",";
  payload += "\"irBeamBCount\":" + String(beamB) + ",";
  payload += "\"irBeamAActive\":" + String(digitalRead(PIN_IR_A) == LOW ? "true" : "false") + ",";
  payload += "\"irBeamBActive\":" + String(digitalRead(PIN_IR_B) == LOW ? "true" : "false") + ",";
  payload += "\"firmware\":\"0.3.0\"";
  payload += "}";
  return payload;
}

void sendTelemetry() {
  loraOk = testLoRa();
  const String payload = buildPayload();
  Serial.println(payload);
  if (WiFi.status() != WL_CONNECTED || strlen(apiUrl) == 0) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(client, apiUrl)) return;
  http.addHeader("Content-Type", "application/json");
  if (strlen(apiKey)) http.addHeader("x-api-key", apiKey);
  const int code = http.POST(payload);
  Serial.printf("Telemetry HTTP status: %d\n", code);
  http.end();
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#if SMARTHIVE_HAS_PRIVATE_WIFI
  WiFi.begin(SMARTHIVE_WIFI_SSID, SMARTHIVE_WIFI_PASSWORD);
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(250);
#else
  prefs.begin("smarthive", false);
  String storedUrl = prefs.getString("apiUrl", DEFAULT_API_URL);
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
  if (wm.autoConnect("SmartHive-Setup")) {
    strlcpy(apiUrl, pUrl.getValue(), sizeof(apiUrl));
    strlcpy(apiKey, pKey.getValue(), sizeof(apiKey));
    strlcpy(deviceId, pDevice.getValue(), sizeof(deviceId));
    prefs.putString("apiUrl", apiUrl);
    prefs.putString("apiKey", apiKey);
    prefs.putString("deviceId", deviceId);
  }
#endif
  Serial.printf("Wi-Fi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "offline");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("SmartHive firmware 0.3.0 booting");
  Wire.begin(PIN_SDA, PIN_SCL);
  shtOk = sht31.begin(0x44);
  gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, HIGH);
  loraSerial.begin(115200, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdOk = SD.begin(PIN_SD_CS, SPI);
  micOk = setupMicrophone();
  pinMode(PIN_IR_A, INPUT);
  pinMode(PIN_IR_B, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IR_A), onIrBeamA, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_IR_B), onIrBeamB, FALLING);
  connectWifi();
  sendTelemetry();
  lastSend = millis();
}

void loop() {
  while (gpsSerial.available()) gps.encode(gpsSerial.read());
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiRetry >= WIFI_RETRY_MS) {
    lastWifiRetry = millis();
    WiFi.reconnect();
  }
  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    lastSend = millis();
    sendTelemetry();
  }
  delay(2);
}
