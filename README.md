# esp8266_arduino_temperature_nodes
Allows a NodeMCU ESP8266 module to be used to capture/display/serve temperature information - uses OLED display and DHT or Dallas type temp sensors

IMPORTANT: I am posting this here for my own as well as for others' benefit. It is my first github public repository. It is also my first foray into the fun and exciting world of ESP8266. Like most of you, I have a job and a family so I may not have a lot of spare time to update this. Having said that, I hope others find some use for this code. Note that I probably snagged bits and pieces of functions from other public places. I will try, best as I can to go over and provide references/links in the code to the places where I got the info from. As of this writing, I haven't had a chance to do so.

This piece of code does potentially interesting things like:
* Accepts and processes HTTP requests.
* Makes HTTP requests to other nodes.
* Makes UDP requests to and parses data from NPT time servers.
* Reads DHT11, DHT22 and Dallas DS temperature sensors.
* Displays data to a 128X64 OLED display.
* Uses ArduinoOTA so that it can be code updated wirelessly.
* Sets up static IP address for ESP8266 node.

This has been written for the ESP8266 Arduino environment.
As I get a chance, I will add some info on the configuration and the specific location of the libraries I'm using. Some of them are ESP8266 specific (the OLED one is for sure). For sure you have to set up your Arduino IDE menu -> Preferences-> Additional Board Managers URLs to "http://arduino.esp8266.com/stable/package_esp8266com_index.json". The Board I selected after that is "NodeMCU 1.0 (ESP=12E Module)". Also note that I've had issues with some NODEMCU modules not programming at any baud rate higher than 115200 but others (that looked identical) from another seller worked up to 921600. Once you get OTA working and you can program wirelessly, baud rate is not an issue.

Good luck to those who wish to replicate this project. Let me know if you run into any gotchas, especially if you solve them, so I can add to the documentaion for anybody else that may follow in your footsteps.
