/* === ESP32 Water Quality System with pH Sensor (No OLED/RGB) === */

#include <AmritDarpan_inferencing.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>

// WiFi
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Sensors
#define TDS_PIN 34
#define TURBIDITY_PIN 35
#define PH_PIN 32

// ISD1820 Audio
#define SAFE_AUDIO_PIN 14
#define UNSAFE_AUDIO_PIN 12

// GPS
HardwareSerial gpsSerial(2);
#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600
String gpsBuffer = "";

// Globals
const float VREF = 3.3;
const int SCOUNT = 30;
int analogBuffer[SCOUNT];
float TDS = 0, NTU = 0, pH = 0;
float features[3] = {0, 0, 0}; // TDS, NTU, pH

// Edge Impulse input
int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

// GPS Reading
void readGPS() {
  gpsBuffer = "";
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    if (c == '\n') break;
    gpsBuffer += c;
  }
}

// Send Notification with GPS
void sendNotification(String msg) {
  readGPS();
  msg += "\nGPS: ";
  msg += gpsBuffer;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin("https://ntfy.sh/AlBENoveRUes");
    http.addHeader("Content-Type", "text/plain");
    int code = http.POST(msg);
    Serial.print("HTTP Response: ");
    Serial.println(code);
    String resp = http.getString();
    Serial.println(resp);
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

// Result Action
void print_inference_result(ei_impulse_result_t result) {
  bool is_safe = result.classification[0].value > result.classification[1].value;

  if (is_safe) {
    digitalWrite(SAFE_AUDIO_PIN, HIGH);
    delay(100);
    digitalWrite(SAFE_AUDIO_PIN, LOW);
    sendNotification("✅ Water is SAFE to drink!");
  } else {
    digitalWrite(UNSAFE_AUDIO_PIN, HIGH);
    delay(100);
    digitalWrite(UNSAFE_AUDIO_PIN, LOW);
    sendNotification("⚠️ Water is NOT SAFE to drink!");
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  pinMode(SAFE_AUDIO_PIN, OUTPUT);
  pinMode(UNSAFE_AUDIO_PIN, OUTPUT);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
}

// Loop
void loop() {
  // TDS
  for (int i = 0; i < SCOUNT; i++) {
    analogBuffer[i] = analogRead(TDS_PIN);
    delay(30);
  }
  int avgValue = 0;
  for (int i = 0; i < SCOUNT; i++) avgValue += analogBuffer[i];
  avgValue /= SCOUNT;
  float tdsVoltage = avgValue * (VREF / 4095.0);
  TDS = (133.42 * pow(tdsVoltage, 3) - 255.86 * pow(tdsVoltage, 2) + 857.39 * tdsVoltage) * 0.5;

  // Turbidity
  int turb = analogRead(TURBIDITY_PIN);
  float turbVolt = turb * (VREF / 4095.0);
  if (turbVolt < 2.5) NTU = (2.5 - turbVolt) * 112.0 + 0.5;
  else NTU = 0;

  // pH
  int phRaw = analogRead(PH_PIN);
  float phVolt = phRaw * (VREF / 4095.0);
  pH = 14.0 * phVolt / VREF;

  // Prepare features
  features[0] = TDS;
  features[1] = NTU;
  features[2] = pH;

  // Run model
  signal_t signal;
  signal.total_length = sizeof(features) / sizeof(features[0]);
  signal.get_data = &raw_feature_get_data;

  ei_impulse_result_t result = { 0 };
  run_classifier(&signal, &result, false);
  print_inference_result(result);

  delay(10000); // Wait 10 seconds
}
