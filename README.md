# CorO₂Sens

Disclaimer and credits: this repository is a fork from https://github.com/kmetz/coro2sens.

Several changes have been made in this fork. Please also consult the README file of the original version.

The following sections have been updated for this repository:



Build a simple device that warns if CO₂ concentration in a room becomes a risk for COVID-19 aerosol infections.

- Measures CO₂ concentration in room air.
- Controls a NeoPixel ring and two LEDs (green + orange/red).
- A buzzer can be connected that alarms if levels are critical.
- Also opens a WiFi portal which shows current readings and a graph (not connected to the internet).
- Can be built for ~ $60 / 50€ (parts cost).

This project was heavily inspired by [ideas from Umwelt-Campus Birkenfeld](https://www.umwelt-campus.de/forschung/projekte/iot-werkstatt/ideen-zur-corona-krise).

You can also find a good overview of the topic by Rainer Winkler here: [Recommendations for use of CO2 sensors to control room air quality during the COVID-19 pandemic](https://medium.com/@rainer.winkler.poaceae/recommendations-for-use-of-co2-sensors-to-control-room-air-quality-during-the-covid-19-pandemic-c04cac6644d0).




## Sensors
The sensor used here is the Sensirion SCD30 (around 70 USD / 60 €).

The pressure compensation by an additional sensor BME280 is currently no longer supported in this forked repo.




## Threshold values
| LED color                 |CO₂ concentration |
|:--------------------------|:----------------------------|
| Green ("all good")        | < 800 ppm                  |
| Yellow ("open windows")   | 800 – 1000 ppm             |
| Red ("leave room")        | \> 1000 ppm                 |

Based on a [Recommendation from the REHVA](https://www.rehva.eu/fileadmin/user_upload/REHVA_COVID-19_guidance_document_V3_03082020.pdf)
(Federation of European Heating, Ventilation and Air Conditioning associations, [rehva.eu](https://www.rehva.eu/))
for preventing COVID-19 aerosol spread, especially in schools.




## Web server
You can read current levels and a simple graph for the last hour by connecting to the WiFi `coro2sens_<serialno>` that is created.
Most devices will open a captive portal, immediately showing the data. You can also open `http://10.0.0.1/` in a browser.




## You need
- A NodeMCU board (any other ESP8266 based board might also work).
- [Sensirion SCD30](https://www.sensirion.com/en/environmental-sensors/carbon-dioxide-sensors/carbon-dioxide-sensors-co2/) I<sup>2</sup>C carbon dioxide sensor module ([mouser](https://mouser.com/ProductDetail/Sensirion/SCD30?qs=rrS6PyfT74fdywu4FxpYjQ==), [digikey](https://www.digikey.com/product-detail/en/sensirion-ag/SCD30/1649-1098-ND/8445334)).
- NeoPixel ring
- *(optional)* 2 LEDs (green + yellow/red); the green one indicates "all good" (< 800 ppm) the other one that this limit has been exceeded
- *(optional)* A 3V piezo buzzer or a small speaker.
- *(optional)* You may want to work with Guido Burger's IoT Octopus PCB. This helps fixing the sensor and the NodeMCU, as well as the LEDs.
- *(optional)* A nice case :) Make sure the sensor has enough air flow.




### Wiring

| ESP8266 pin | goes to                                    |
| :---------- | :----------------------------------------- |
| 3V3         | SCD30 VIN                                  |
| GND         | SCD30 GND, Buzzer (-)                      |
| SCL / D1    | SCD30 SCL                                  |
| SDA / D2    | SCD30 SDA                                  |
| D3          | LED DIN                                    |
| D5          | Green LED                                  |
| D6          | Buzzer (+)                                 |
| D7          | Warning LED (yellow or red is a good idea) |
| D8          | NeoPixel ring                              |

