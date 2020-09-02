# CorO<sub>2</sub>Sens

Build a simple device that warns if CO<sub>2</sub> concentration in a room becomes a risk for Covid-19 aerosol infections.

- Measures CO<sub>2</sub> concentration in room air.
- Controls an RGB LED (green, yellow, red, like a traffic light).
- A buzzer can be connected that alarms if levels are critical.


## Threshold values
| LED color                 |CO<sub>2</sub> concentration |
|:--------------------------|:----------------------------|
| Green ("all good")        | < 1000 ppm                  |
| Yellow ("open windows")   | 1000 – 2000 ppm             |
| Red ("leave room")        | \> 2000 ppm                 |

Based on [ideas from Umwelt-Campus Birkenfeld](https://www.umwelt-campus.de/forschung/projekte/iot-werkstatt/ideen-zur-corona-krise), which are based on 2008 [research by the German Federal Environmental Agency](https://www.umweltbundesamt.de/sites/default/files/medien/pdfs/kohlendioxid_2008.pdf).


## Web server
You can read current levels and a simple graph for the last hour by connecting to the WiFi `coro2sens` that is created.
Most devices will open a captive portal, immediately showing the data. You can also open `http://10.0.0.1/` in a browser.


## You need
1. Any ESP32 or ESP8266 board (like a WEMOS D32 or WEMOS D1 Mini).
1. Sensirion SCD30 carbon dioxide sensor module ([mouser.com](https://mouser.com/ProductDetail/Sensirion/SCD30?qs=rrS6PyfT74fdywu4FxpYjQ==)).
1. 1 NeoPixel compatible RGB LED (WS2812B). 
1. Optional: BME280 I<sup>2</sup>C pressure sensor module, improves accuracy.   
1. Optional: 3V piezo buzzer or simple speaker.
1. A nice case :)


### Wiring

| ESP pin      | goes to                                    |
|:-------------|:-------------------------------------------|
| 3V3          | SCD30 VIN, BME280 VIN                      |
| 5V           | LED +5V                                    |
| GND          | SCD30 GND, BME280 GND, LED GND, Buzzer (-) |
| SCL / D1     | SCD30 SCL, BME280 SCL                      |
| SDA / D2     | SCD30 SDA, BME280 SDA                      |
| GPIO 0 / D3  | LED DIN                                    |
| GPIO 14 / D5 | Buzzer (+)                                 |


(GPIOs can easily be changed in `src/main.cpp`)


### Flashing the ESP using PlatfomIO
- Simply open the project and upload.
- Or via command line: `platformio run -t upload`

### Flash using the Arduino IDE
- Rename `src/main.cpp` to `coro2sense.ino` and place it in a folder named `coro2sense`.
- Open `coro2sense.ino` in the Arduino IDE.
- Install all libraries mentioned in `platformio.ini` (the `lib_deps` section) using the library manager.
- Upload (hope it works).
