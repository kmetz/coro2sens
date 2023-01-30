// SETUP =======================================================================

/* Platform and feature specific configuration */
#include "config.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

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

#ifdef SETTINGS_STORAGE
  #include <EEPROM.h>
#endif

struct settings_t {
  uint32_t magic;
  uint16_t co2_ppm_warn;
  uint16_t co2_ppm_alarm;
  uint16_t co2_ppm_critical;
  uint8_t neopixel_brightness;
  uint16_t buzzer_tone_freq_hz;
  uint16_t buzzer_tone_duration_ms;
  uint16_t buzzer_max_num_beeps;
  char hotspot_name[WIFI_HOTSPOT_SIZE];
} settings;

SCD30 scd30;
uint16_t co2 = 0;
unsigned long lastMeasureTime = 0;
bool alarmHasTriggered = false;
uint16_t alarmActiveCount = 0;
uint16_t co2log[LOG_SIZE] = {0}; // Ring buffer.
uint32_t co2logPos = 0; // Current buffer start position.
uint16_t co2logDownsample = std::max(1, ((((LOG_MINUTES) * 60) / MEASURE_INTERVAL_S) / LOG_SIZE));
uint16_t co2avg, co2avgSamples = 0; // Used for downsampling.
byte mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xC0, 0xFE}; // MAC address Wifi shield

#ifdef NEOPIXEL_PIN
  Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
  // Note: could handle dynamic strip length; setter pixels.updateLength() + stored setting
#endif

#if WIFI_ENABLED
  AsyncWebServer server(80);
  IPAddress apIP(WIFI_AP_IP);
  IPAddress netMsk(WIFI_AP_NETMASK);
  DNSServer dnsServer;
  void handleCaptivePortal(AsyncWebServerRequest *request);
  void handleConfig(AsyncWebServerRequest *request);
#endif

/* List function prototypes */
void indicate_calib(void);
void set_green_led(int value);
void set_yellow_led(int value);
void set_red_led(int value);
void set_warn_led(int value);
void init_leds(void);
void alarmSound();

/**
   Triggered continuously when the CO2 level is critical.
*/
void alarmSound()
{
#if defined(BUZZER_PIN)
  serial_printf("alarmActiveCount: %d\n", alarmActiveCount);
  // beep only up to a specific amount of times after alarm threshold was hit
  if( alarmActiveCount <= settings.buzzer_max_num_beeps )
  {
#if defined(ESP32)
    // Use Tone32.
    tone(BUZZER_PIN, settings.buzzer_tone_freq_hz, settings.buzzer_tone_duration_ms, 0);
#else
    // Use Arduino tone().
    tone(BUZZER_PIN, settings.buzzer_tone_freq_hz, settings.buzzer_tone_duration_ms);
#endif
  }
#endif /* defined(BUZZER_PIN) */
}

void store_current_settings()
{
#ifdef SETTINGS_STORAGE
  EEPROM.put(0, settings);
  EEPROM.commit();
#endif
}

void load_settings()
{
#ifdef SETTINGS_STORAGE
  EEPROM.begin(sizeof(settings_t));
  serial_printf("EEPROM length: %d\n", EEPROM.length());
  EEPROM.get(0, settings);

  serial_printf("settings.magic: 0x%08X\n", settings.magic);

  // EEPROM has never been written before, store default values;
  // also set the following line to `if(false)` when restructuring the data structure (data sizes or order); expanding shouldn't be a problem
  //if(false)
  if(SETTINGS_EEPROM_MAGIC == settings.magic)
  {
    serial_printf("Settings have been loaded from EEPROM");
  }
  else
  {
#endif
    // Do some "maehgic"
    settings.magic = SETTINGS_EEPROM_MAGIC;
    // Use default settings
    settings.co2_ppm_warn = CO2_WARN_PPM;
    settings.co2_ppm_alarm = CO2_CRITICAL_PPM;
    settings.co2_ppm_critical = CO2_CRITICAL_PPM;
    settings.neopixel_brightness = LED_INTENSITY;
    settings.buzzer_tone_freq_hz = BEEP_TONE_FREQ;
    settings.buzzer_tone_duration_ms = BEEP_DURATION_MS;
    settings.buzzer_max_num_beeps = BUZZER_MAX_BEEPS;
    snprintf(settings.hotspot_name, WIFI_HOTSPOT_SIZE, "%s_%02X%02X%02X%02X%02X%02X", WIFI_HOTSPOT_PREFIX, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    serial_printf("Settings have been loaded from defaults");
#ifdef SETTINGS_STORAGE
    // Store default settings in EEPROM
    store_current_settings();
    serial_printf(" and been stored in EEPROM");
  }
#endif
  serial_printf(".\n");
}

void setup()
{
#if USE_SERIAL_CONSOLE
  serial_begin(SERIAL_BAUDRATE);
  serial_println(); // start with line break to get rid of gibberish output after flashing
#endif

#if WIFI_ENABLED
  // Overwrite dummy MAC address with real data
  WiFi.macAddress(mac);
  serial_printf("WiFi is enabled (MAC: %02X%02X%02X%02X%02X%02X)\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif

  load_settings();

  serial_println("settings:");
  serial_printf(" .magic:                   0x%08X\n", settings.magic);
  serial_printf(" .co2_ppm_warn:            %d\n", settings.co2_ppm_warn);
  serial_printf(" .co2_ppm_alarm:           %d\n", settings.co2_ppm_alarm);
  serial_printf(" .co2_ppm_critical:        %d\n", settings.co2_ppm_critical);
  serial_printf(" .buzzer_tone_freq_hz:     %d\n", settings.buzzer_tone_freq_hz);
  serial_printf(" .buzzer_tone_duration_ms: %d\n", settings.buzzer_tone_duration_ms);
  serial_printf(" .buzzer_max_num_beeps:    %d\n", settings.buzzer_max_num_beeps);
  serial_printf(" .hotspot_name:            %s\n", settings.hotspot_name);

#ifdef NEOPIXEL_PIN
  serial_printf(" .neopixel_brightness:     %d\n", settings.neopixel_brightness);

  // Initialize NeoPixel strip's LED(s).
  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  pixels.clear(); // Set all pixel colors to 'off'
#endif

  // Initialize GPIOs for discrete LEDs.
  init_leds();

  // initialize digital pin for the button as input.
  pinMode(BUTTON_PIN, INPUT);

  // Initialize buzzer.
#ifdef BUZZER_PIN
  pinMode(BUZZER_PIN, OUTPUT);
#endif

  delay(2000);

  // Initialize SCD30 sensor.
  Wire.begin();
  if(scd30.begin(Wire))
  {
    serial_println("SCD30 CO2 sensor detected.");
  }
  else
  {
    serial_println("SCD30 CO2 sensor not detected. Please check wiring. Freezing.");
    // Light up all discrete LEDs to indicate HW error.
    set_green_led(HIGH);
    set_yellow_led(HIGH);
    set_red_led(HIGH);
    set_warn_led(HIGH);
    indicate_calib(); // Also use the NeoPixel strip.
    delay(UINT32_MAX);
  }
  scd30.setMeasurementInterval(MEASURE_INTERVAL_S);

#if WIFI_ENABLED
  // Initialize WiFi, DNS and web server.
  serial_println("Starting WiFi hotspot ...");
  //serial_println( WiFi.macAddress() );
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(settings.hotspot_name);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(WIFI_DNS_PORTNO, "*", apIP);
  serial_printf("WiFi hotspot started (\"%s\")\n", settings.hotspot_name);

  server.on("/", HTTP_GET, handleCaptivePortal);
#ifdef SETTINGS_STORAGE
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfig);
#endif
  server.onNotFound(handleCaptivePortal);
  server.begin();
#endif
}

void set_pixel_by_co2(uint16_t co2_ppm)
{
#ifdef NEOPIXEL_PIN
  static int num_leds_old = 0;
  static int num_leds = 0;
  static uint32_t colorval = pixels.Color(COLOR_BLACK);
  num_leds = co2_ppm / 100; /* 1600 max., 16 pixels --> 100 ppm/pixel; FIXME: static HW dependency! */
  num_leds = (num_leds > NUMPIXELS) ? NUMPIXELS : num_leds;

  //serial_printf("num_leds: %d.\n", num_leds); // only for debugging

  /* avoid flickering, so switch off pixels only if number of LEDs switched on has decreased */
  if (num_leds_old > num_leds)
  {
    pixels.clear(); // Set all pixel colors to 'off'
  }
  num_leds_old = num_leds;

  /* Select color for all pixels */
  if (co2_ppm < settings.co2_ppm_warn)
  {
    colorval = pixels.Color(COLOR_GREEN);
  }
  else if (co2_ppm >= settings.co2_ppm_warn && co2_ppm <= settings.co2_ppm_critical)
  {
    colorval = pixels.Color(COLOR_YELLOW);
  }
  else
  {
    colorval = pixels.Color(COLOR_RED);
  }

  for (int i = 0; i < num_leds; i++)
  {
    pixels.setPixelColor(i, colorval);    
  }
  pixels.show(); // Send the updated pixel colors to the hardware (after the loop).
#endif

  /* if at least one LED is used */
#if defined(LED_GREEN_PIN) || defined(LED_YELLOW_PIN) || defined(LED_RED_PIN)
  if (co2_ppm < settings.co2_ppm_warn)
  {
    set_green_led(HIGH);
    set_yellow_led(LOW);
    set_red_led(LOW);
    set_warn_led(LOW);
  }
  else if (co2_ppm >= settings.co2_ppm_warn && co2_ppm <= settings.co2_ppm_critical)
  {
    set_green_led(LOW);
    set_yellow_led(HIGH);
    set_red_led(LOW);
    set_warn_led(HIGH);
  }
  else
  {
    set_green_led(LOW);
    set_yellow_led(LOW);
    set_red_led(HIGH);
    set_warn_led(HIGH);
  }
#endif
}

void init_leds(void)
{
#ifdef LED_GREEN_PIN
  pinMode(LED_GREEN_PIN, OUTPUT);
  set_green_led(HIGH);
#endif
#ifdef LED_RED_PIN
  pinMode(LED_RED_PIN, OUTPUT);
  set_red_led(HIGH);
#endif
#ifdef LED_YELLOW_PIN
  pinMode(LED_YELLOW_PIN, OUTPUT);
  set_yellow_led(HIGH);
#endif
#ifdef LED_WARN_PIN
  pinMode(LED_WARN_PIN, OUTPUT);
  set_warn_led(HIGH);
#endif
}

void set_green_led(int value)
{
#ifdef LED_GREEN_PIN
  digitalWrite(LED_GREEN_PIN, value);
#endif
}

void set_yellow_led(int value)
{
#ifdef LED_YELLOW_PIN
  digitalWrite(LED_YELLOW_PIN, value);
#endif
}

void set_red_led(int value)
{
#ifdef LED_RED_PIN
  digitalWrite(LED_RED_PIN, value);
#endif
}

void set_warn_led(int value)
{
#ifdef LED_WARN_PIN
  digitalWrite(LED_WARN_PIN, value);
#endif
}

void indicate_calib(void)
{
#ifdef NEOPIXEL_PIN
  pixels.clear(); // Set all pixel colors to 'off'
  for (int i = 0; i < NUMPIXELS; i++)
  {
    if( i % 3 == 0 )
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_GREEN));
    }
    else if( i % 3 == 1 )
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_YELLOW));
    }
    else
    {
      pixels.setPixelColor(i, pixels.Color(COLOR_RED));
    }
  }
  pixels.show();   // Send the updated pixel colors to the hardware.
#endif
}

void loop()
{
  static uint8_t nLoopCnt = 0;

  // Tasks that need to run continuously.
#if WIFI_ENABLED
  dnsServer.processNextRequest();
#endif

  // Early exit.
  if ((millis() - lastMeasureTime) < (MEASURE_INTERVAL_S * 1000))
  {
    return;
  }

#if WIFI_ENABLED
  if( ++nLoopCnt == 20 )
  {
    serial_printf("WiFi hotspot available (\"%s\")\n", settings.hotspot_name);
    nLoopCnt = 0;
  }
#endif

  if( LOW == digitalRead(BUTTON_PIN) )
  {
    Serial.print("Start SCD 30 calibration, please wait 30 s ...");
    indicate_calib();
    delay(30000);
    scd30.setAutoSelfCalibration(false); // deactivate self-calibration; setting is stored in non-volatile memory
    delay(1000);
    scd30.setForceRecalibration(410); // set to fresh air, estimate 410 ppm as a reference (400 ppm is the minimum for the function parameter)
    delay(1000);
    pixels.clear();
  }

  if( scd30.dataAvailable() )
  {
    co2 = scd30.getCO2();

    if( co2 < CO2_PLASABILIT_PPM_MIN ) // do not use implausible values, concentration is unlikely to be < CO2_PLASABILIT_PPM_MIN
    {
      serial_printf("[WARN] Implausible CO2 value: %d\n", co2);
      co2 = CO2_PLASABILIT_PPM_MIN;
    }
  }

  // Average (downsample) and log CO2 values for the graph.
  co2avg = ((co2avgSamples * co2avg) + co2) / (co2avgSamples + 1);
  co2avgSamples++;
  if (co2avgSamples >= co2logDownsample)
  {
    co2log[co2logPos] = co2avg;
    co2logPos++;
    co2logPos %= LOG_SIZE;
    co2avg = co2avgSamples = 0;
  }

  // Print all sensor values to the serial console.
  serial_printf(
    "[SCD30]  temp: %.2f°C, humid: %.2f%%, CO2: %dppm\r\n",
    scd30.getTemperature(), scd30.getHumidity(), co2
  );
  serial_println("-----------------------------------------------------");

  // Update LED(s).
  set_pixel_by_co2(co2);

  // Handle alarms (trigger, reset)
  if (co2 >= settings.co2_ppm_alarm) {
    /* rising edge detection for trigger */
    if (!alarmHasTriggered)
    {
      alarmHasTriggered = true;
    }
    alarmActiveCount++;
    alarmSound();
  }
  else
  {
    /* falling edge detection for trigger */
    if( alarmHasTriggered)
    {
      alarmHasTriggered = false;
    }

    /* reset active timer value */
    alarmActiveCount = 0;
  }

  lastMeasureTime = millis();
}


#if WIFI_ENABLED
/**
   Handle requests for the captive portal.
   @param request
*/
void handleCaptivePortal(AsyncWebServerRequest *request)
{
  serial_println("handleCaptivePortal");
  AsyncResponseStream *res = request->beginResponseStream("text/html");

  res->print("<!DOCTYPE html><html><head>\n");
  res->print("<title>coro2sens</title>\n");
  res->print(R"(<meta content="width=device-width,initial-scale=1" name="viewport">)");
  res->printf(R"(<meta http-equiv="refresh" content="%d">)", std::max(MEASURE_INTERVAL_S, 10));
  res->print(R"(<style type="text/css">* { font-family:sans-serif }</style>)");
  res->print("</head><body>\n");

  // Print request URL for debugging
  //res->printf("<p>Webpage at %s</p>", request->url().c_str());

  // Current measurement.
  res->printf(R"(<h1><span style="color:%s">&#9679;</span> %d ppm CO<sub>2</sub></h1>)",
              (co2 >= settings.co2_ppm_critical) ? "red" : ((co2 >= settings.co2_ppm_warn) ? "yellow" : "green"), co2);

  // Generate SVG graph.
  uint16_t maxVal = settings.co2_ppm_critical + (settings.co2_ppm_critical - settings.co2_ppm_warn);
  for (uint16_t val : co2log)
  {
    if (val > maxVal)
    {
      maxVal = val;
    }
  }
  uint w = GRAPH_W, h = GRAPH_H, x, y;
  uint16_t val;
  res->printf(R"(<svg width="100%%" height="100%%" viewBox="0 0 %d %d">)", w, h);
  // Background.
  res->printf(R"(<rect style="fill:#FFC1B0; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, 0, w, (int) map(maxVal - settings.co2_ppm_critical, 0, maxVal, 0, h));
  res->printf(R"(<rect style="fill:#FFFCB3; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, (int) map(maxVal - settings.co2_ppm_critical, 0, maxVal, 0, h), w, (int) map(settings.co2_ppm_warn, 0, maxVal, 0, h));
  res->printf(R"(<rect style="fill:#AFF49D; stroke:none" x="%d" y="%d" width="%d" height="%d"/>)",
              0, (int) map(maxVal - settings.co2_ppm_warn, 0, maxVal, 0, h), w, (int) map(settings.co2_ppm_warn, 0, maxVal, 0, h));
  // Threshold values.
  res->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">&ge; %d ppm</text>)",
              4, (int) map(maxVal - settings.co2_ppm_critical, 0, maxVal, 0, h) - 6, settings.co2_ppm_critical);
  res->printf(R"(<text style="color:black; font-size:10px" x="%d" y="%d">&leq; %d ppm</text>)",
              4, (int) map(maxVal - settings.co2_ppm_warn, 0, maxVal, 0, h) + 12, settings.co2_ppm_warn);
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
  res->printf("<p>%s</p>\n", TIME_LABEL);

  res->print("</body></html>");
  request->send(res);
}

bool getPostUintParam(AsyncWebServerRequest *request, const String& name, uint16_t& value, const uint16_t min, const uint16_t max)
{
  if(request->hasParam(name, true))
  {
    serial_printf("has parameter '%s'", name);
    uint16_t tmpValue = std::atoi(request->getParam(name, true)->value().c_str());
    if( (tmpValue >= min) && (tmpValue <= max) )
    {
      value = tmpValue;
      return true;
    }
  }
  return false;
}

#ifdef SETTINGS_STORAGE
/**
   Handle requests for config page.
   @param request
*/
void handleConfig(AsyncWebServerRequest *request)
{
  serial_println("handleConfig");
  AsyncResponseStream *res = request->beginResponseStream("text/html");

  int params = request->params();
  for (int i = 0; i < params; i++) {
    AsyncWebParameter* p = request->getParam(i);
    serial_printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
  }

  res->print("<!DOCTYPE html><html>\n<head>");
  res->print("<title>coro2sens config</title>");
  res->print(R"(<style type="text/css">)");
  res->print("\n* { font-family:sans-serif }");
  res->print("\n.form {display: table;}");
  res->print("\n.formrow {display: table-row;}");
  res->print("\n.col {display: table-cell; padding: 6px;}");
  res->print("\n.notification {display: table-cell; padding: 16px; color:white; background-color:#49a9f9;}");
  res->print("\n</style>\n</head>\n<body>\n");
  res->print("<h1>Settings</h1>\n");

  // Check if we got any params, i.e. most likely the form has been submit (but could be from both POST and GET method)!
  if(params > 0)
  {
    bool settingsChanged = false; // is set to true when a change is detected later on
    bool printedNotification = false;

    if(request->hasParam("hotspot_name", true))
    {
      if( 0 != strncmp(request->getParam("hotspot_name", true)->value().c_str(), settings.hotspot_name, WIFI_HOTSPOT_SIZE ) )
      {
        strncpy( settings.hotspot_name, request->getParam("hotspot_name", true)->value().c_str(), WIFI_HOTSPOT_SIZE-1 );
        settings.hotspot_name[WIFI_HOTSPOT_SIZE-1] = '\0';
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Hotspot name has changed. Changes are adopted after reboot (re-power the device)!<br />\n");
      }
    }

    uint16_t co2_ppm_warn;
    if( getPostUintParam(request, "co2_ppm_warn", co2_ppm_warn, CO2_WARN_PPM_MIN, CO2_WARN_PPM_MAX) )
    {
      if( settings.co2_ppm_warn != co2_ppm_warn )
      {
        settings.co2_ppm_warn = co2_ppm_warn;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("CO2 warning level (ppm) setting has changed.<br />\n");
      }
    }

    uint16_t co2_ppm_critical;
    if( getPostUintParam(request, "co2_ppm_critical", co2_ppm_critical, CO2_CRITICAL_PPM_MIN, CO2_CRITICAL_PPM_MAX) )
    {
      if( settings.co2_ppm_critical != co2_ppm_critical )
      {
        settings.co2_ppm_critical = co2_ppm_critical;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Critical CO2 level (ppm) setting has changed.<br />\n");
      }
    }

    uint16_t neopixel_brightness;
    if( getPostUintParam(request, "neopixel_brightness", neopixel_brightness, LED_INTENSITY_MIN, LED_INTENSITY_MAX) )
    {
      if( settings.neopixel_brightness != neopixel_brightness )
      {
        settings.neopixel_brightness = neopixel_brightness;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("NeoPixel LED brightness setting has changed.<br />\n");
      }
    }

    uint16_t co2_ppm_alarm;
    if( getPostUintParam(request, "co2_ppm_alarm", co2_ppm_alarm, CO2_CRITICAL_PPM_MIN, CO2_CRITICAL_PPM_MAX) )
    {
      if( settings.co2_ppm_alarm != co2_ppm_alarm )
      {
        settings.co2_ppm_alarm = co2_ppm_alarm;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Buzzer alarm threshold setting has changed.<br />\n");
      }
    }

    uint16_t buzzer_tone_freq_hz;
    if( getPostUintParam(request, "buzzer_tone_freq_hz", buzzer_tone_freq_hz, BEEP_TONE_FREQ_MIN, BEEP_TONE_FREQ_MAX) )
    {
      if( settings.buzzer_tone_freq_hz != buzzer_tone_freq_hz )
      {
        settings.buzzer_tone_freq_hz = buzzer_tone_freq_hz;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Buzzer tone frequency setting has changed.<br />\n");
      }
    }

    uint16_t buzzer_tone_duration_ms;
    if( getPostUintParam(request, "buzzer_tone_duration_ms", buzzer_tone_duration_ms, BEEP_DURATION_MS_MIN, BEEP_DURATION_MS_MAX) )
    {
      if( settings.buzzer_tone_duration_ms != buzzer_tone_duration_ms )
      {
        settings.buzzer_tone_duration_ms = buzzer_tone_duration_ms;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Buzzer tone duration setting has changed.<br />\n");
      }
    }

    uint16_t buzzer_max_num_beeps;
    if( getPostUintParam(request, "buzzer_max_num_beeps", buzzer_max_num_beeps, BUZZER_MAX_BEEPS_MIN, BUZZER_MAX_BEEPS_MAX) )
    {
      if( settings.buzzer_max_num_beeps != buzzer_max_num_beeps )
      {
        settings.buzzer_max_num_beeps = buzzer_max_num_beeps;
        settingsChanged = true;
        if( !printedNotification )
        {
          res->print(R"(<div class="notification">)");
          printedNotification = true;
        }
        res->print("Number of buzzer beeps during an alarm setting has changed.<br />\n");
      }
    }

    if( settingsChanged )
    {
      if( !printedNotification )
      {
        res->print(R"(<div class="notification">)");
        printedNotification = true;
      }
      res->print("Storing new setting(s) to EEPROM.<br />\n");
      store_current_settings();
    }

    if( printedNotification )
    {
      res->print("</div>\n"); // end notification div
    }
  }

  // (Re-)Load settings to be displayed
  load_settings();

  res->print(R"(<form method="post">)");
  
  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">MAC address</div>)");
  char mac_cstr[20];
  snprintf(mac_cstr, 20, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  res->printf(R"(  <div class="col">%s</div>)", mac_cstr);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Hotspot name</div>)");
  res->printf(R"(  <div class="col"><input type="text" name="hotspot_name" value="%s" size="30" maxlength="%d"></div>)", settings.hotspot_name, WIFI_HOTSPOT_SIZE-1);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Warning level</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="co2_ppm_warn" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;ppm</div>)", 
    settings.co2_ppm_warn, CO2_WARN_PPM_MIN, CO2_WARN_PPM_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(<div class="col">Critical level</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="co2_ppm_critical" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;ppm</div>)",
    settings.co2_ppm_critical, CO2_CRITICAL_PPM_MIN, CO2_CRITICAL_PPM_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(<div class="col">NeoPixel LED brightness</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="neopixel_brightness" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;&#37;</div>)",
    settings.neopixel_brightness, LED_INTENSITY_MIN, LED_INTENSITY_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Buzzer alarm threshold</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="co2_ppm_alarm" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;ppm</div>)",
    settings.co2_ppm_alarm, CO2_CRITICAL_PPM_MIN, CO2_CRITICAL_PPM_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Buzzer tone frequency</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="buzzer_tone_freq_hz" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;Hz</div>)",
    settings.buzzer_tone_freq_hz, BEEP_TONE_FREQ_MIN, BEEP_TONE_FREQ_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Buzzer tone duration</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="buzzer_tone_duration_ms" value="%d" min="%d" max="%d" size="6" maxlength="4">&nbsp;ms per beep</div>)",
    settings.buzzer_tone_duration_ms, BEEP_DURATION_MS_MIN, BEEP_DURATION_MS_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col">Number of buzzer beeps</div>)");
  res->printf(R"(  <div class="col"><input type="number" name="buzzer_max_num_beeps" value="%d" min="%d" max="%d" size="6" maxlength="2"> (0=muted)</div>)",
    settings.buzzer_max_num_beeps, BUZZER_MAX_BEEPS_MIN, BUZZER_MAX_BEEPS_MAX);
  res->print("</div>\n");

  res->print(R"(<div class="formrow">)");
  res->print(R"(  <div class="col"></div>)");
  res->print(R"(  <div class="col"><input type="submit" value="save"></div>)");
  res->print("</div>\n");

  res->print("</form>\n</body>\n</html>\n");

  request->send(res);
}
#endif /* SETTINGS_STORAGE */
#endif /* WIFI_ENABLED */
