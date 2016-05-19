# esp8266_arduino_temperature_nodes
Allows a NodeMCU ESP8266 module to be used to capture/display/serve temperature information - uses OLED display and DHT or Dallas type temp sensors

It does things like:
* Wait for and process HTTP requests.
* Makes HTTP requests to other nodes.
* Makes UDP requests to NPT time servers.
* Reads DHT11, DHT22 and Dallas DS temperature sesnsors.
* Displays data to an 128X64 OLED display.
* Uses ArduinoOTA so that it can be code updated wirelessly.
* Sets up static IP for ESP8266 node.

This has been written for the ESP8266 Arduino environment.
