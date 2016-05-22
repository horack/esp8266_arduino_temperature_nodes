# esp8266_arduino_temperature_nodes
Allows a NodeMCU ESP8266 module to be used to capture/display/serve temperature information - uses OLED display and DHT or Dallas type temp sensors

NOTE: I am posting this here for my own as well as for others' benefit. It is my first github public repository. It is also my first foray into the fun and exciting world of ESP8266. Like most of you, I have a job and a family so I may not have a lot of spare time to update this. Having said that, I hope others find some use for this code. Note that I probably snagged bits and pieces of functions from other public places. I have tried, best as I can, to go over and provide references/links in the code to the places where I got the info from. Some similarities to other code might be coincidental, but if it looks too familiar, it's possible I missed a place where I should have provided a link so let me know and I'll fix it.

This piece of code does potentially interesting things like:
* Accepts and processes HTTP requests.
* Makes HTTP requests to other nodes.
* Makes UDP requests to and parses data from NPT time servers.
* Reads DHT11, DHT22 and Dallas DS temperature sensors.
* Displays data to a 128X64 OLED display (optional)
* Displays data to an ILI9341 TFT touch display optional, and no touch functionality implemented yet)
* Uses ArduinoOTA so that it can be code updated wirelessly.
* Sets up static IP address for ESP8266 node.

Please read through all the code, especially the comments, for information on how to set up some of the functionality and how to connect the hardware. It is important to set up the proper IP. Afterwards, if you start programming the devices wirelessly, make sure you're set up for the right IP in code define as well as the remote PORT you're programming to.

This has been written for the ESP8266 Arduino environment.
Note that some variants of the libraries need to be ESP8266 specific. I have tried to add URLs to thos libraries. For sure you also have to set up your Arduino IDE menu -> Preferences-> Additional Board Managers URLs to "http://arduino.esp8266.com/stable/package_esp8266com_index.json" so that you can code for ESP8266. The Board I selected after that is "NodeMCU 1.0 (ESP=12E Module)". Also note that I've had issues with some NODEMCU modules not programming at any baud rate higher than 115200 but others (that looked identical) from another seller worked up to 921600. Once you get OTA working and you can program wirelessly, baud rate is not an issue. Currently I have also started seeing the module stop waiting for SSID connection after an program upload, and even resetting won't get it to move past that point. The only option is to disconnect/reconnect the USB power.

A little info on hardware: I am using an I2C OLED 128x64 OLED display with an SSD1306 chip. I have also added support for an ILI9341 TFT touch screen display. Look at the comments to enable/disable display options and to find out how to hook up the hardware. Both flavors of display can be powered from the NODEMCU's 3.3V pins. Currently, you can have both types of display connected at the same time and the display functions will just mirror the output to both of them. Note that the code can be used w/o any display since you can see your temperature data through a browser. Just point the browser to the appropriate IP and port 8484. To get a simple data string that's easy to parse, you can add a "/data" to your URL. You can even remote reboot the module by sending a "/reset" to the URL.

Good luck to those who wish to replicate this project. Let me know if you run into any gotchas, especially if you solve them, so I can add to the documentaion for anybody else that may follow in your footsteps.
