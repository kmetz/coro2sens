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


// Buzzer, activated continuously when CO2 level is critical.
#if defined(ESP32)
#define BUZZER_PIN 19
#elif defined(ESP8266)
#define BUZZER_PIN D4
#endif
#define BEEP_DURATION_MS 100 // Beep milliseconds
#define BEEP_TONE 1047 // C6

// BME280 pressure sensor (optional).
// Address should be 0x76 or 0x77.
//#define BME280_I2C_ADDRESS 0x76

// Update CO2 level every MEASURE_INTERVAL_S seconds.
// Should be kept at 2 unless you want to save power.
#define MEASURE_INTERVAL_S 2

// WiFi.
// Set to 0 to disable altogether.
#define WIFI_ENABLED 1

// 1 = captive portal hotspot without internet connection, showing data when you connect with it.
// 0 = WiFi client, showing data when accessed via IP address.
#define WIFI_HOTSPOT_MODE 1

// AP name when WIFI_HOTSPOT_MODE is 1
#define WIFI_HOTSPOT_NAME "coro2sens"

// Credentials when WIFI_HOTSPOT_MODE is 0
#define WIFI_CLIENT_SSID "your WiFi name"
#define WIFI_CLIENT_PASSWORD "*****"

// How long the graph/log in the WiFi portal should go back, in minutes.
#define LOG_MINUTES 60
// Label describing the time axis.
#define TIME_LABEL "1 hour"

// Activity indicator LED (use the built-in LED if your board has one).
// Which pin on the Arduino is connected to the NeoPixels?
#define NEOPIXEL_PIN   D4
#undef NEOPIXEL_PIN

#define OCTOPUS          1

#if defined(OCTOPUS)
#define LED_GREEN_PIN    D5
#else
#define LED_GREEN_PIN    D8
#endif
#define LED_YELLOW_PIN   D7
#define LED_RED_PIN      D6


// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      16

#define LED_INTENSITY  5
#define COLOR_GREEN    0, 3*LED_INTENSITY, 0
#define COLOR_YELLOW   2*LED_INTENSITY, LED_INTENSITY, 0
#define COLOR_RED      3*LED_INTENSITY, 0, 0
// =============================================================================


#define GRAPH_W 600
#define GRAPH_H 260
#define LOG_SIZE GRAPH_W


#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SparkFunBME280.h>

#if defined(ESP32)
#include <SparkFun_SCD30_Arduino_Library.h>
#include <Tone32.h>
#if WIFI_ENABLED
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#endif

#elif defined(ESP8266)
#include <paulvha_SCD30.h>
#if WIFI_ENABLED
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#endif

#if WIFI_ENABLED
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#endif


SCD30 scd30;
uint16_t co2 = 0;
unsigned long lastMeasureTime = 0;
bool alarmHasTriggered = false;
uint16_t co2log[LOG_SIZE] = {0}; // Ring buffer.
uint32_t co2logPos = 0; // Current buffer start position.
uint16_t co2logDownsample = max(1, ((((LOG_MINUTES) * 60) / MEASURE_INTERVAL_S) / LOG_SIZE));
uint16_t co2avg, co2avgSamples = 0; // Used for downsampling.

#ifdef NEOPIXEL_PIN
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

BME280 bme280;
bool bme280isConnected = false;
uint16_t pressure = 0;

#if WIFI_ENABLED
AsyncWebServer server(80);
IPAddress apIP(10, 0, 0, 1);
IPAddress netMsk(255, 255, 255, 0);
DNSServer dnsServer;
void handleCaptivePortal(AsyncWebServerRequest *request);
#endif


/**
   Triggered once when the CO2 level goes critical.
*/
void alarmOnce() {
}


/**
   Triggered continuously when the CO2 level is critical.
*/
void alarmContinuous() {
#if defined(ESP32)
  // Use Tone32.
  tone(BUZZER_PIN, BEEP_TONE, BEEP_DURATION_MS, 0);
#else
  // Use Arduino tone().
  tone(BUZZER_PIN, BEEP_TONE, BEEP_DURATION_MS);
#endif
}


void setup() {
  Serial.begin(115200);

  // Initialize LED(s).
#ifdef NEOPIXEL_PIN
  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
#endif

  // Initialize buzzer.
  pinMode(BUZZER_PIN, OUTPUT);

#ifdef LED_GREEN_PIN
  pinMode(LED_GREEN_PIN, OUTPUT);
  digitalWrite(LED_GREEN_PIN, HIGH);
#endif
#ifdef LED_RED_PIN
  pinMode(LED_RED_PIN, OUTPUT);
  digitalWrite(LED_RED_PIN, HIGH);
#endif
#ifdef LED_YELLOW_PIN
  pinMode(LED_YELLOW_PIN, OUTPUT);
  digitalWrite(LED_YELLOW_PIN, HIGH);
#endif

  delay(2000);

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
  //  bme280.setI2CAddress(BME280_I2C_ADDRESS);
  //  if (bme280.beginI2C(Wire)) {
  //    Serial.println("BMP280 pressure sensor detected.");
  //    bme280isConnected = true;
  //    // Settings.
  //    bme280.setFilter(4);
  //    bme280.setStandbyTime(0);
  //    bme280.setTempOverSample(1);
  //    bme280.setPressureOverSample(16);
  //    bme280.setHumidityOverSample(1);
  //    bme280.setMode(MODE_FORCED);
  //  }
  //  else {
  //    Serial.println("BMP280 pressure sensor not detected. Please check wiring. Continuing without ambient pressure compensation.");
  //  }

#if WIFI_ENABLED
  // Initialize WiFi, DNS and web server.
#if WIFI_HOTSPOT_MODE
  Serial.println("Starting WiFi hotspot ...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(WIFI_HOTSPOT_NAME);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  Serial.printf("WiFi hotspot started (\"%s\")\r\n", WIFI_HOTSPOT_NAME);
#else
  Serial.println("Connecting WiFi ...");
  WiFi.begin(WIFI_CLIENT_SSID, WIFI_CLIENT_PASSWORD);
  uint timeout = 30;
  while (timeout > 0 && WiFi.status() != WL_CONNECTED) {
    delay(1000);
    timeout--;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected (%s).\r\n", WiFi.localIP().toString().c_str());
  }
  else {
    Serial.println("WiFi connection failed.");
  };
#endif
  server.on("/", HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);
  server.begin();
#endif
}

void set_pixel_by_co2(uint16_t co2_ppm)
{
  static int num_leds_old = 0;
  int num_leds = 0;
  num_leds = co2_ppm / 100; /* 1600 max., 16 pixels --> 100 ppm/pixel */
  num_leds = (num_leds > 16) ? 16 : num_leds;

  //Serial.printf("num_leds: %d.\r\n", num_leds);

#ifdef NEOPIXEL_PIN
  /* avoid flickering */
  if (num_leds_old > num_leds)
  {
    pixels.clear(); // Set all pixel colors to 'off'
  }
  num_leds_old = num_leds;

  for (int i = 0; i < num_leds; i++)
  {
    if (co2_ppm < CO2_WARN_PPM)
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_GREEN));
    }
    else if (co2_ppm >= CO2_WARN_PPM && co2_ppm <= CO2_CRITICAL_PPM)
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_YELLOW));
    }
    else
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_RED));
    }
    pixels.show();   // Send the updated pixel colors to the hardware.
  }
#endif

#if defined(LED_GREEN_PIN) && defined(LED_YELLOW_PIN) && defined(LED_RED_PIN)
    if (co2_ppm < CO2_WARN_PPM)
    {
      digitalWrite(LED_GREEN_PIN, HIGH);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED_PIN, LOW);
    }
    else if (co2_ppm >= CO2_WARN_PPM && co2_ppm <= CO2_CRITICAL_PPM)
    {
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, HIGH);
      digitalWrite(LED_RED_PIN, LOW);
    }
    else
    {
      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED_PIN, HIGH);
    }
#endif

}

void loop() {
  // Tasks that need to run continuously.
#if WIFI_ENABLED && WIFI_HOTSPOT_MODE
  dnsServer.processNextRequest();
#endif

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

  // Update LED(s).
  set_pixel_by_co2(co2);

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


#if WIFI_ENABLED
/**
   Handle requests for the captive portal.
   @param request
*/
void handleCaptivePortal(AsyncWebServerRequest *request) {
  Serial.println("handleCaptivePortal");
  AsyncResponseStream *res = request->beginResponseStream("text/html");

  res->print("<!DOCTYPE html><html><head>");
  res->print("<title>coro2sens</title>");
  res->print(R"(<meta content="width=device-width,initial-scale=1" name="viewport">)");
  res->printf(R"(<meta http-equiv="refresh" content="%d">)", max(MEASURE_INTERVAL_S, 10));
  res->print(R"(<style type="text/css">* { font-family:sans-serif }</style>)");
  res->print("</head><body>");

  // Current measurement.
  res->printf(R"(<h1><span style="color:%s">&#9679;</span> %d ppm CO<sub>2</sub></h1>)",
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
  res->printf(R"(<svg width="100%%" height="100%%" viewBox="0 0 %d %d">)", w, h);
  // Background.
  res->printf(R"(<rect style="fill:#FFC1B0; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, 0, w, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h));
  res->printf(R"(<rect style="fill:#FFFCB3; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  res->printf(R"(<rect style="fill:#AFF49D; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h), w, (int) map(CO2_WARN_PPM, 0, maxVal, 0, h));
  // Threshold values.
  res->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">> %d ppm</text>)",
              4, (int) map(maxVal - CO2_CRITICAL_PPM, 0, maxVal, 0, h) - 6, CO2_CRITICAL_PPM);
  res->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">< %d ppm</text>)",
              4, (int) map(maxVal - CO2_WARN_PPM, 0, maxVal, 0, h) + 12, CO2_WARN_PPM);
  // Plot line.
  res->print(R"(<path style="fill:none; stroke:black; stroke-width:2px; stroke-linejoin:round" d=")");
  for (uint32_t i = 0; i < LOG_SIZE; i += (LOG_SIZE / w)) {
    val = co2log[(co2logPos + i) % LOG_SIZE];
    x = (int) map(i, 0, LOG_SIZE, 0, w + (w / LOG_SIZE));
    y = h - (int) map(val, 0, maxVal, 0, h);
    res->printf("%s%d,%d", i == 0 ? "M" : "L", x, y);
  }
  res->print(R"("/>)");
  res->print("</svg>");

  // Labels.
  res->printf("<p>%s</p>", TIME_LABEL);

  res->print("</body></html>");
  request->send(res);
}
#endif
