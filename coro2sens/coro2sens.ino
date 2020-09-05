#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SparkFunBME280.h>

#if defined(ESP32)

#include <SparkFun_SCD30_Arduino_Library.h>
#include <Tone32.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#elif defined(ESP8266)
#include <paulvha_SCD30.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>


// SETUP =======================================================================

// CO2 Thresholds (ppm).
//
// Recommendation from REHVA (Federation of European Heating, Ventilation and Air Conditioning associations, rehva.eu)
// for preventing COVID-19 aerosol spread especially in schools:
// - warn: 800, critical: 1000
// (https://www.rehva.eu/fileadmin/user_upload/REHVA_COVID-19_guidance_document_V3_03082020.pdf)
//
// General air quality recommendation by the German Federal Environmental Agency (2008):
// - warn: 1000, critical: 2000
// (https://www.umweltbundesamt.de/sites/default/files/medien/pdfs/kohlendioxid_2008.pdf)
//
#define CO2_WARN_PPM 800
#define CO2_CRITICAL_PPM 1000

// LED warning light (always on, green / yellow / red).
#define LED_PIN 0
#define LED_BRIGHTNESS 37

// Buzzer, activated continuously when CO2 level is critical.
#define BUZZER_PIN 14
#define BEEP_DURATION_MS 100
#define BEEP_TONE 1047 // C6

// Switch, is pulled HIGH once for SWITCH_DURATION_MS when CO2 level becomes critical.
#define SWITCH_PIN 12
#define SWITCH_DURATION_MS 200

// BME280 pressure sensor (optional).
// Address should be 0x76 or 0x77.
#define BME280_I2C_ADDRESS 0x76

// Update CO2 level every MEASURE_INTERVAL_S seconds.
// Can range from 2 to 1800.
#define MEASURE_INTERVAL_S 2

// WiFi captive portal showing sensor values.
// Set to 0 to disable.
#define WIFI_PORTAL_ENABLED 1
#define WIFI_AP_NAME "coro2sens"

// How long the graph/log in the WiFi portal should go back, in minutes.
#define LOG_MINUTES 60
// Label describing the time axis.
#define TIME_LABEL "1 hour"

// =============================================================================


#define GRAPH_W 400
#define GRAPH_H 200
#define LOG_SIZE GRAPH_W


SCD30 scd30;
uint16_t co2 = 0;
unsigned long lastMeasureTime = 0;
bool alarmHasTriggered = false;
uint16_t co2log[LOG_SIZE] = {0}; // Ring buffer.
uint32_t co2logPos = 0; // Current buffer start position.
uint16_t co2logDownsample = max(1, ((((LOG_MINUTES) * 60) / MEASURE_INTERVAL_S) / LOG_SIZE));
uint16_t co2avg, co2avgSamples = 0; // Used for downsampling.

BME280 bme280;
bool bme280isConnected = false;
uint16_t pressure = 0;

Adafruit_NeoPixel led = Adafruit_NeoPixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);

AsyncWebServer server(80);
IPAddress apIP(10, 0, 0, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;


/**
 * Triggered once when the CO2 level goes critical.
 */
void alarmOnce() {
  digitalWrite(SWITCH_PIN, HIGH);
  delay(SWITCH_DURATION_MS);
  digitalWrite(SWITCH_PIN, LOW);
}


/**
 * Triggered continuously when the CO2 level is critical.
 */
void alarmContinuous() {
  tone(BUZZER_PIN, BEEP_TONE, BEEP_DURATION_MS);
  digitalWrite(LED_PIN, LOW);
}


/**
 * Handle requests for the captive portal.
 * @param request
 */
void handleCaptivePortal(AsyncWebServerRequest *request) {
  Serial.println("handleCaptivePortal");
  AsyncResponseStream *response = request->beginResponseStream("text/html");

  response->print("<!DOCTYPE html><html><head>");
  response->print("<title>coro2sens</title>");
  response->print(R"(<meta content="width=device-width,initial-scale=1" name="viewport">)");
  response->printf(R"(<meta http-equiv="refresh" content="%d">)", max(MEASURE_INTERVAL_S, 10));
  response->print(R"(<style type="text/css">* { font-family:sans-serif }</style>)");
  response->print("</head><body>");

  // Current measurement.
  response->printf(R"(<h1><span style="color:%s">&#9679;</span> %d ppm CO<sub>2</sub></h1>)",
                   co2 > CO2_CRITICAL_PPM ? "red" : co2 > CO2_WARN_PPM ? "yellow" : "green", co2);

  // Generate SVG graph.
  uint16_t maxVal = CO2_CRITICAL_PPM + (CO2_CRITICAL_PPM - CO2_WARN_PPM);
  for (uint16_t val : co2log) {
    if (val > maxVal) {
      maxVal = val;
    }
  }
  uint w = GRAPH_W, h = GRAPH_H, x, y;
  uint16_t val;
  response->printf(R"(<svg width="100%%" height="100%%" viewBox="0 0 %d %d">)", w, h);
  // Background.
  response->printf(R"(<rect style="fill:#FFC1B0; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, 0, w, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h));
  response->printf(R"(<rect style="fill:#FFFCB3; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  response->printf(R"(<rect style="fill:#AFF49D; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
                   0, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  // Threshold values.
  response->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">> %d ppm</text>)",
                   4, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h) - 6, CO2_CRITICAL_PPM);
  response->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">< %d ppm</text>)",
                   4, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h) + 12, CO2_WARN_PPM);
  // Plot line.
  response->print(R"(<path style="fill:none; stroke:black; stroke-width:2px" d=")");
  for (uint32_t i = 0; i < LOG_SIZE; i += (LOG_SIZE / w)) {
    val = co2log[(co2logPos + i) % LOG_SIZE];
    x = (int) map(i, 0, LOG_SIZE, 0, w + (w / LOG_SIZE));
    y = h - (int) map(val, 0, maxVal, 0, h);
    response->printf("%s%d,%d", i == 0 ? "M" : "L", x, y);
  }
  response->print(R"("/>)");
  response->print("</svg>");

  // Labels.
  response->printf("<p>%s</p>", TIME_LABEL);

  response->print("</body></html>");
  request->send(response);
}


void setup() {
  Serial.begin(115200);

  // Initialize pins.
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SWITCH_PIN, OUTPUT);

  // Initialize LED.
  led.begin();
  led.setBrightness(LED_BRIGHTNESS);
  led.setPixelColor(0, 0, 0, 0);
  led.show();

  // Initialize SCD30 sensor.
  Wire.begin();
  if (scd30.begin(Wire)) {
    Serial.println("SCD30 CO2 sensor detected.");
  }
  else {
    Serial.println("SCD30 CO2 sensor not detected. Please check wiring. Freezing.");
    delay(UINT32_MAX);
  }
  scd30.setMeasurementInterval(MEASURE_INTERVAL_S);

  // Initialize BME280 sensor.
  bme280.setI2CAddress(BME280_I2C_ADDRESS);
  if (bme280.beginI2C(Wire)) {
    Serial.println("BMP280 pressure sensor detected.");
    bme280isConnected = true;
    // Settings.
    bme280.setFilter(4);
    bme280.setStandbyTime(0);
    bme280.setTempOverSample(1);
    bme280.setPressureOverSample(16);
    bme280.setHumidityOverSample(1);
    bme280.setMode(MODE_FORCED);
  }
  else {
    Serial.println("BMP280 pressure sensor not detected. Please check wiring. Continuing without ambient pressure compensation.");
  }

  // Initialize WiFi, DNS and web server.
  if (WIFI_PORTAL_ENABLED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(WIFI_AP_NAME);
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    server.on("/", HTTP_GET, handleCaptivePortal);
    server.onNotFound(handleCaptivePortal);
    server.begin();
  }
}


void loop() {
  // Tasks that need to run continuously.
  if (WIFI_PORTAL_ENABLED) {
    dnsServer.processNextRequest();
  }

  // Early exit.
  if ((millis() - lastMeasureTime) < (MEASURE_INTERVAL_S * 1000)) {
    return;
  }

  // Read sensors.
  if (bme280isConnected) {
    pressure = (uint16_t) (bme280.readFloatPressure() / 100);
    scd30.setAmbientPressure(pressure);
  }
  if (scd30.dataAvailable()) {
    co2 = scd30.getCO2();
  }

  // Average (downsample) and log CO2 values for the graph.
  co2avg = ((co2avgSamples * co2avg) + co2) / (co2avgSamples + 1);
  co2avgSamples++;
  if (co2avgSamples >= co2logDownsample) {
    co2log[co2logPos] = co2avg;
    co2logPos++;
    co2logPos %= LOG_SIZE;
    co2avg = co2avgSamples = 0;
  }

  // Print all sensor values.
  Serial.printf(
    "[SCD30]  temp: %.2f°C, humid: %.2f%%, CO2: %dppm\r\n",
    scd30.getTemperature(), scd30.getHumidity(), co2
  );
  if (bme280isConnected) {
    Serial.printf(
      "[BME280] temp: %.2f°C, humid: %.2f%%, press: %dhPa\r\n",
      bme280.readTempC(), bme280.readFloatHumidity(), pressure
    );
  }
  Serial.println("-----------------------------------------------------");

  // Update LED.
  if (co2 < CO2_WARN_PPM) {
    led.setPixelColor(0, 0, 255, 0); // Green.
  }
  else if (co2 < CO2_CRITICAL_PPM) {
    led.setPixelColor(0, 255, 255, 0); // Yellow.
  }
  else {
    led.setPixelColor(0, 255, 0, 0); // Red.
  }
  led.show();

  // Trigger alarms.
  if (co2 >= CO2_CRITICAL_PPM) {
    alarmContinuous();
    if (!alarmHasTriggered) {
      alarmOnce();
      alarmHasTriggered = true;
    }
  }
  if (co2 < CO2_CRITICAL_PPM && alarmHasTriggered) {
    alarmHasTriggered = false;
  }

  lastMeasureTime = millis();
}
