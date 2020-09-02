#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SparkFunBME280.h>
#ifdef ESP32
#include <SparkFun_SCD30_Arduino_Library.h>
#include <Tone32.h>
#else
#include <paulvha_SCD30.h>
#endif


// SETUP -----------------------------------------

// LED (always on).
// - Green: all good (CO2 level < 1000 ppm).
// - Yellow: warning, open windows (> 1000 ppm).
// - Red: critical, leave room (> 2000 ppm).
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

// -----------------------------------------------


SCD30 scd30;
Adafruit_NeoPixel led = Adafruit_NeoPixel(1, LED_PIN, NEO_GRB + NEO_KHZ800);
BME280 bme280;
bool bme280isConnected = false;
uint16_t co2 = 0;
uint16_t pressure = 0;
bool alarmHasTriggered = false;


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
}


void loop() {
  // Read sensors.
  if (bme280isConnected) {
    pressure = (uint16_t) (bme280.readFloatPressure() / 100);
    scd30.setAmbientPressure(pressure);
  }
  if (scd30.dataAvailable()) {
    co2 = scd30.getCO2();
  }

  Serial.printf(bme280isConnected ? "%d ppm, %d hPa\r\n" : "%d ppm\r\n", co2, pressure);

  // Update LED.
  if (co2 < 1000) {
    led.setPixelColor(0, 0, 255, 0); // Green.
  }
  else if (co2 < 2000) {
    led.setPixelColor(0, 255, 255, 0); // Yellow.
  }
  else {
    led.setPixelColor(0, 255, 0, 0); // Red.
  }
  led.show();

  // Trigger alarms.
  if (co2 >= 2000) {
    alarmContinuous();
    if (!alarmHasTriggered) {
      alarmOnce();
      alarmHasTriggered = true;
    }
  }
  if (co2 < 2000 && alarmHasTriggered) {
    alarmHasTriggered = false;
  }

  delay(MEASURE_INTERVAL_S * 1000);
}
