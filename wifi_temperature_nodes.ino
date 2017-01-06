// This code can be found at: https://github.com/horack/esp8266_arduino_temperature_nodes
// It has been build with snippets of code from many places, I've tried to provide links wherever I remembered the source...

#include <Adafruit_ILI9341.h> // using version 1.0.2 (1.0.1 does not have esp8266 support, https://github.com/adafruit/Adafruit_ILI9341 )
#include <Adafruit_GFX.h> // using version 1.1.5
#include <SPI.h> // needed for TFT display
#include <Fonts/FreeSerifBold18pt7b.h> // (for TF, find other fonts in Adafruit_GFX/Fonts folder)
// These font settings have to match to the font you've included above
#define THE_FONT &FreeSerifBold18pt7b
#define FONT_HEIGHT FreeSerifBold18pt7b.yAdvance
#define FONT_OFFSET 10

// Both of these are included when you pick the Time library in Arduino IDE library manager
#include <Time.h>
#include <TimeLib.h>
// These come from ESP8266 libs I get when I use http://arduino.esp8266.com/stable/package_esp8266com_index.json in Arduino Preferences for Additional Board Manager URLs
// to set up Arduino for ESP8266 development.
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// This is an ESP8266 specific SSD1306 library (which shows up as "ESP8266 OLED Driver for SSD1306 display" in Library Manager)
#include <SSD1306.h> // this variant comes from https://github.com/adafruit/Adafruit-SSD1331-OLED-Driver-Library-for-Arduino

// Include appropriate temperature sensor lib here
#include <DHT.h> // Adafruit DHT (NOT Unified DHT) library ( https://github.com/adafruit/DHT-sensor-library )
#include <DallasTemperature.h> // ( https://github.com/milesburton/Arduino-Temperature-Control-Library )
#include <OneWire.h>
#define DS18 18 // pseudo value for Dallas temperature sensors

// ***************************************************************************************************************************************************************
// ***************************************************************************************************************************************************************
// These are defines you'll want to change depending on which node you're programming, also look for AP_SSID and AP_PASSWORD
// More info on these values and others further down, readl all comments (even if some MIGHT be out of date)
#define IP_LAST_OCTET 40 // This defines this node's static IP, read on for more info (also find array NODEMCU_NODES)
#define TEMP_TYPE DHT22 // set this to DS18 for Dallas 3 pin sensors, or DHT22 or DTH11 for the DHT 4 pin sensors
#define USE_OLED // Uncomment if you're using an SSD1306 type 128x64 OLED display (currently set up for I2C version)
//#define USE_TFT // Uncomment if you're using an ILI9341 type TFT display (you can have both OLED and TFT hooked up at the same time, display functions currently will mirror the data)
// find Graphics section below to get pinout information for OLED and/or TFT module
// ***************************************************************************************************************************************************************
// When this code runs, you can query your module over http at: http://192.168.1.40:8484 (the ip may be different depending on your changes and network)
// you can also get a more machine-readable version of the data by browsing to: http://192.168.1.40:8484/data 
// ***************************************************************************************************************************************************************

// Apparently the Dn NodeMCU pins are no longer defined, so here they are
#ifndef D1
#define D0 16 // cannot use for interrupts
#define D1 5
#define D2 4
#define D3 0
#define D4 2
#define D5 14 // this is also SCLK
#define D6 12 // this is also MISO
#define D7 13 // this is also MOSI
#define D8 15 // this is also CS
#define D9 3
#define D10 1
#endif

// This program will assign static IPs to your module(s). In my case my internal IPs are configured by the wifi router as 192.168.1.XXX
// Note that you should make sure that the NODEMCU_NODES array (further down in code) does contain an entry for whatever value you set IP_LAST_OCTET to
// -------------------------------------------------------------------------------------------------------------------------------------------------
// NOTE: Always make sure you set up IP_LAST_OCTET correctly when you burn OTA (wirelessly), because if you're selecting one specific device over-the-air, but will be burning a different IP to it, it can cause you confusion,
//       especially if there is already another node online with that same IP.
// --------------------------------------------------------------------------------------------------------------------------------------------------

// NOTE: this is a path to my own accesspoint.h file, that contains my router's SSID and password, I am keeping this private.
//       You can
//         EITHER create your own file and change the #include path below to point to it (due to Arduino IDE it must be an absolute path, and
//           copy the accessinfo.h contents listed between the comment lines and change values as needed)
//         OR you can just comment out the #include and just plug in your SSID and password values directly into the AP_SSID and AP_PASSWORD defines
//           I include this accesspoint.h in multiple projects, so I prefer having this common file.
#include "D:/projects/Arduino/ESP8266/accesspoint.h"
// start of accessinfo.h contents ----------------------------
#ifndef AP_INFO_H
  #define AP_INFO_H
  #define AP_SSID	"your_wifi_router_SSID_here"
  #define AP_PASSWORD "your_accesspoint_password"
#endif // AP_INFO_H
// end of accessinfo.h contents ------------------------------
#define PORT 8484 // This is the port that the node will listen on
WiFiServer server(PORT);

// Here's info that needs to match you local WiFi configuration. Make sure the info is correct for your set up.
// You can probably get this info by examining your computer's wifi network configuration (in Windows type "ipconfig" in a command line window)
// Note that the static IP is based on value of IP_LAST_OCTET
const IPAddress myIp(192, 168, 1, IP_LAST_OCTET);
const IPAddress gateway(192, 168, 1, 1);
const IPAddress subnet(255, 255, 255, 0);
const IPAddress dns1(192, 168, 1, 1);
const IPAddress dns2(192, 168, 1, 1);

// I am defining a namespace here since it appears to be the easiest way to get structs defined in Arduino without needing separate files
namespace codeus {
	// A SLAVE_RESULT contains info received from a slave
	typedef struct SLAVE_RESULT {
		time_t lastUpdateEpoch;
		String name;
		String temperature;
		String humidity;
	};

	// A NODEMCU_NODE has info specific to a node in this master/slave pseudo network
	typedef struct NODEMCU_NODE {
		bool isMaster;
		byte lastIPOctet;
		char* defaultName;
		SLAVE_RESULT* result; // this will be populated with the slave's results when the master queries it
	};
	
}
// This array is a list of possible NodeMCU nodes I MAY have running, not all of them need to be running and available.
// Note that only one should have the isMaster flag set to true, it will run in master mode and query the other nodes for updates.
// In this particular example, if I have set IP_LAST_OCTET to 41, that means I am compiling for a master node (the "Master LR" node in the list has octet 41 and isMaster true).
// The nodes start out with the defined names in this list for each IP. You can change the names in the list and you can also change a name remotely, over HTTP.
// Make sure this array has an entry for whatever you set IP_LAST_OCTET to at compile time !!!!
// You can add to, or remove from this list and name your modules whatever you want, you can even have duplicate names, but do not duplicate the IP octet values.
// TODO: perhaps save node info in a local "file" in the node's flash
// TODO: change code to allow slave nodes to ping a master and add or remove themselves dynamically
codeus::NODEMCU_NODE NODEMCU_NODES[] = {
	{true,  40, "Livingroom", NULL},
	{false, 41, "SW Room", NULL}, // this node is flagged as the master, you can move the 'true' flag to another node, if you want
	{false, 42, "Mel", NULL},
	{false, 43, "Cold Room", NULL},
	{false, 44, "Garage", NULL},
	{false, 45, "Outside", NULL}
};
#define NODEMCU_NODE_COUNT sizeof NODEMCU_NODES / sizeof NODEMCU_NODES[0]

// general purpose function to locate a node by IP octet in the above list
codeus::NODEMCU_NODE* findNode(byte lastIPOctet) {
	codeus::NODEMCU_NODE* result = NULL;
	for (int i = 0; i < NODEMCU_NODE_COUNT; i++) {
		if (NODEMCU_NODES[i].lastIPOctet == lastIPOctet) {
			result = &NODEMCU_NODES[i];
		}
	}
	return result;
}

const codeus::NODEMCU_NODE* ThisNode = findNode(IP_LAST_OCTET); // In the code, ThisNode will contain a pointer to this specific node's info (determined by IP_LAST_OCTET at build time)
String nodeName = String(ThisNode->defaultName);

// time stuff -------------------------------------------------------------------------------------
bool firstTimeGot = false;
String timeStr = "NO-TIME-SET";
String dateStr = "NO-DATE-SET";
// the following 2 ints are just for diagnostic purposes to see how many times we tried to reach NPT and succeeded (and how far)
int nptGets = 0;
int nptAttempts = 0;

// Temperature sensor stuff -------------------------------------------------------------------------------------
const long tempInterval = 5000; // interval at which to read sensor
unsigned long previousTempMillis = tempInterval; // will store last temp was read
#define TEMP_PIN	D1 // data pin for sensor

// Initialize temperature sensor
#if TEMP_TYPE == DS18
	OneWire oneWire(TEMP_PIN);
	DallasTemperature DS18B20(&oneWire);
#else
// NOTE: For working with a faster than ATmega328p 16 MHz Arduino chip, like an ESP8266,
// you need to increase the threshold for cycle counts considered a 1 or 0.
// You can do this by passing a 3rd parameter for this threshold.	It's a bit
// of fiddling to find the right value, but in general the faster the CPU the
// higher the value.	The default for a 16mhz AVR is a value of 6.	For an
// Arduino Due that runs at 84mhz a value of 30 works.
// This is for the ESP8266 processor on ESP-01 
	DHT dht(TEMP_PIN, TEMP_TYPE, 11); // 11 works fine for ESP8266
#endif
String tempStr = "--.-"; // displayable temperature string
String humStr = "--.-"; // displayable humidity string, for Dallas sensors this will remain as --.- since they don't have humidity sensing
float humidity, temp_f;	// Values read from sensor


// Graphics -----------------------------------------------------------------------------------------------------------------------------
#ifdef USE_OLED
// OLED 128x64 displays
	// Haven't tested SPI OLED module, so you're on your own
	//#define OLED_RESET	D0	// RESET
	//#define OLED_DC	D2	// Data/Command
	//#define OLED_CS	D8	// Chip select
	// SSD1306 display(true, OLED_RESET, OLED_DC, OLED_CS); // FOR SPI

	// Pin connections for I2C OLED
	// OLED pin -> NODEMCU pin
	// VCC -> any 3.3V NODEMCU pin
	// GND -> any NODEMCU GND
	// SCL -> D4 (GPIO2)
	// SDA -> D2 (GPIO4)
	#define OLED_SDA	D2
	#define OLED_SCL	D4
	#define OLED_ADDR	0x3C // I2C address for OLED, some might use 3D
	SSD1306	display(OLED_ADDR, OLED_SDA, OLED_SCL);	// For I2C
#endif //USE_OLED

#ifdef USE_TFT
// This is for the cheap touch screen ILI9341 based TFT display you find on Amazon and Ebay (less than $10), typically 320x240
// Touch functionality not yet used in this code (I'll add it some day)
	// Pin connections for ILI9341 TFT (note that any 3.3V can be shared, same for GND)
	// ILI9341 pin -> NODEMCU pin
	// VCC -> any NODEMCU 3.3V pin
	// GND -> any NODEMCU GND
	// D/C -> D3
	// CS - > D8
	// SDO(MISO) -> D6
	// SDI(MOSI) -> D7
	// SCK -> D5
	// LED -> any 3.3V NODEMCU pin
	// RESET -> any 3.3V NODEMCU pin
	#define TFT_DC D3
	#define TFT_CS D8
	Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);
	#define BACKGROUND_COLOR ILI9341_BLACK
	#define TOP_TEXT_COLOR ILI9341_YELLOW
	#define TEXT_COLOR 0x05FF
	#define STATUS_COLOR ILI9341_RED
	#define TEXT_SIZE 1
	#define ORIENTATION 1 // 2 is upside-down portrait
#endif //USE_TFT
#define ESP_SPI_FREQ 4000000

// Master node stuff -------------------------------------------------------------------------------------
#define SLAVE_CHECK_INTERVAL_SEC 60 // interval at which to scan slaves, don't make it too fast, scanning nodes is slow especially if some nodes are not online and responding
time_t slaveCheckNext = 0;
int getsCount = 0;
int bytesCount = 0;
int attemptsCount = 0;

// INIT functions -------------------------------------------------------------------------------------
void initDisplay() {
	// display stuff
#ifdef USE_TFT
	SPI.setFrequency(ESP_SPI_FREQ);
	tft.begin();
	tft.setRotation(ORIENTATION); // I flip display because of how I have the board in my setup, you may or may not need/want to flip
	tft.fillScreen(BACKGROUND_COLOR);
	tft.setFont(THE_FONT);
#endif //USE_TFT
#ifdef USE_OLED
	display.init();
	display.flipScreenVertically(); // I flip display because of how I have the board in my setup, you may or may not need/want to flip
#endif //USE_OLED
}

void connectWifiAccessPoint(String ssid, String password) {
	WiFi.reconnect();
	WiFi.mode(WIFI_STA);
	WiFi.config(myIp, gateway, subnet, dns1, dns2);
	displayAll("Begin SSID:\n" + ssid);
	char ssidCC[60];
	char passwordCC[60];
	ssid.toCharArray(ssidCC, (unsigned int)ssid.length() + 1);
	password.toCharArray(passwordCC, (unsigned int)password.length() + 1);
	WiFi.begin(ssidCC, passwordCC);
	displayAll("Wait for SSID:\n" + String(ssid));
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {
		displayAll("Connection Failed!\nRebooting...");
		delay(5000);
		ESP.restart();
	}
}

// SETUP function -------------------------------------------------------------------------------------
void setup() {
	Serial.begin(115200);
	delay(100);
	initDisplay();

	displayAll("Booting");

	// Connect to WiFi network
	displayAll("Connecting SSID:\n" + String(AP_SSID));
	connectWifiAccessPoint(AP_SSID, AP_PASSWORD);
	displayAll("WiFi connected");

// ArduinoOTA setup, for the very cool over-the-air wireless code update ability
// Theoretically, once this code is programmed into a nodeMCU over USB, you can afterwards re-program it wirelessly. Just make sure
// your module is running before starting Arduino IDE then you should should see it in the programming ports menu as something like:
// "NODEMCU-192-168-1-n at 192.168.1.n (Generic ESP8266 Module)"    where n is our IP octet defined in IP_LAST_OCTET
// There are times where you will need to program over USB again, for example if the code you just updated wirelessly has a bug in how
// it sets up ArduinoOTA, or gets hung up in a loop where the ArduinoOTA.handle() is not being called periodically anymore.
// -------------------------------------------------------------------------------------------------------------------------------------------------
// NOTE: Always make sure you set up IP_LAST_OCTET correctly when you burn OTA, because if you're selecting one specific device over-the-air, but will be burning a different IP to it, it can cause you confusion,
//       especially if there is already another node online with that same IP.
// --------------------------------------------------------------------------------------------------------------------------------------------------
	String otahostStr = "NODEMCU-192-168-1-" + String(IP_LAST_OCTET); // this is the name you'll see available in the Arduino->Tools->Port menu for programming this device wirelessly
	char otahost[100];
	otahostStr.toCharArray(otahost, (unsigned int)otahostStr.length() + 1);
	ArduinoOTA.setHostname(otahost);
	ArduinoOTA.onStart([]() { // this function gets called when you initiate a wireless code update, we'll just display info
		displayAll("OTA Starting.");
	});
	ArduinoOTA.onEnd([]() { // this gets called when the code upload completes
		displayAll("OTA Complete.");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { // this gets called periodically as the code upload progresses
		int percent = (progress / (total / 100));
		displayAll("OTA Prog: " + String(percent) + "%");
	});
	ArduinoOTA.onError([](ota_error_t error) { // this gets called if there's an upload error
		String theError = "OTA Error[%u]: " + String(error) + "\n";
		if (error == OTA_AUTH_ERROR) {
			theError = theError + "Auth Failed";
		} else if (error == OTA_BEGIN_ERROR) {
			theError = theError + "Begin Failed";
		} else if (error == OTA_CONNECT_ERROR) {
			theError = theError + "Connect Failed";
		} else if (error == OTA_RECEIVE_ERROR) {
			theError = theError + "Receive Failed";
		} else if (error == OTA_END_ERROR) {
			theError = theError + "End Failed";
		}
		displayAll(theError);
	});
	ArduinoOTA.begin(); // initialize the OTA module (note that in the program loop we will have to call ArduinoOTA.handle() so it can listen for update requests

	// Start the server
	server.begin();
	Serial.println("Server started");
 
	// Print the IP address
	displayAll("My HTTP host:\n"
			"IP: " +  WiFi.localIP().toString() + "\n"
			"Port: " + String(PORT));
	delay(2000);
}

// LOOP function -------------------------------------------------------------------------------------
void loop() {
	ArduinoOTA.handle();

	updateTimeFromServer(); // sync our internal clock periodically with real time from remote server (also gets actual time rather than relative from boot time)
	updateTemperature();

// ***************************** if we're a Master node check slaves here
	if (ThisNode->isMaster && slaveCheckNext <= now()) { // am I a master and is it time to collect data from slaves ?
		collectSlaveData();
	}
 
// ************************** check and handle request for data from a browser
	WiFiClient client = server.available();
	String request = acceptRequest(client);
	if (request.length() > 0) { // we have a request
		String payload = "";
		if (request.indexOf("/reset") >= 0) { // this allows you to reset a node
			ESP.restart();
		} else if (request.indexOf("/data") >= 0) { // this allows you to get temp data in a more easily machine readable format (used by a master to get slaves' data)
			payload = "[[temperature[" + tempStr + "]][[humidity[" + humStr + "]][name[" + nodeName + "]][time[" + timeStr + "]][date[" + dateStr + "]][uptime[" + (millis() / 1000) + "]]";
		} else { // a regular request formats the data in a human readable page
			int textPtr = request.indexOf("text=");
			Serial.println("textPtr = " + textPtr);
			if (textPtr >= 0) { // if you submit text from the human readable page, you can change the node's name from defaultName
				int endPtr = request.indexOf("&", textPtr);
				if (endPtr == -1) {
					endPtr = request.indexOf(" ", textPtr);
				}
				Serial.println("endPtr = " + endPtr);
				nodeName = request.substring(textPtr + 5, endPtr);
				nodeName.replace("+", " ");
				Serial.println("text = " + nodeName);
			}
			String allNodes = "";
			if (ThisNode->isMaster) {
				// if we're master add in all the responding slave nodes' data for display
				allNodes = "";
				for (int i = 0; i < NODEMCU_NODE_COUNT; i++) {
					codeus::NODEMCU_NODE* node = &NODEMCU_NODES[i];
					//TODO: build response from all nodes
					if (node->result != NULL && node->lastIPOctet != IP_LAST_OCTET) {
						codeus::SLAVE_RESULT* res = node->result;
						allNodes = allNodes + buildNodeHtml(res->name, res->temperature, res->humidity, node->lastIPOctet, 	now() - res->lastUpdateEpoch) + "<br>";
					}
				}
			}

			// this is the webapge we'll render to the browser
			payload =
				"<!DOCTYPE HTML><html>"
				"<br><br>"
				"<font size='16' color='black'>Time: " + timeStr + "</font><br>"
				"<font size='16' color='black'>Date: " + dateStr + "</font><br>" +
				"<form action='/' method='get'><input type='text' name='text'/><input type='submit' value='Rename'/></form><br>" +
				buildNodeHtml(nodeName, tempStr, humStr, IP_LAST_OCTET, 0) + "<br>" +
				allNodes +
				"</html>"
				"";
		}
			
		// Return the response
		client.println(
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"\r\n" //	do not forget this one
			+ payload +
			"\r\n\r\n");
		
		delay(1);
		Serial.println("Client disconnected");
		Serial.println("");
	} // end client check
	
	// display stuff to the OLED/TFT too
	displayText(
		tempStr + "F" + "  " + "h:" + humStr + "%" + "\n" +
		nodeName + "\n" +
		dateStr + "\n" +
		timeStr + " \n" +
		""
	);
}
String buildNodeHtml(String nStr, String tStr, String hStr, int octet, time_t secondsSinceReading) {
	return
		"<font size='16' color='blue'><b>" + nStr + "</b>:</font>&nbsp;&nbsp;"
		"<font size='16' color='red'>" + tStr + "&deg;</font>&nbsp;&nbsp;"
		"<font size='16' color='green'>" + hStr +"% rh</font>" +
		"<font size='14' color='black'>[" + String(octet) + "] " + String(secondsSinceReading) +" seconds</font>";
}

void collectSlaveData() {
	slaveCheckNext = now() + SLAVE_CHECK_INTERVAL_SEC;
	for (int i = 0; i < NODEMCU_NODE_COUNT; i++) {
		codeus::NODEMCU_NODE* node = &NODEMCU_NODES[i];
		if (node->lastIPOctet != IP_LAST_OCTET) { // skip ourselves
			String url = "http://192.168.1." + String(node->lastIPOctet) + ":" + PORT + "/data";
			String n = node->defaultName;
			if (node->result != NULL && node->result->name != NULL) {
				n = node->result->name;
			}
			displayTextStatus(">: " + n);
			String res = getHttpPayload(url, 1000);
			if (res != NULL) {
				String n = extractSlaveValue(res, "name");
				String t = extractSlaveValue(res, "temperature");
				String h = extractSlaveValue(res, "humidity");
				node->result = new codeus::SLAVE_RESULT {now(), n, t, h};
			}
		}
	}		
	displayTextStatus("");
}

// parses a slave's returned data to extract a certain field, data is in format of: [fieldname1[fieldvalue1]][fieldname2[fieldvalue2]]...
String extractSlaveValue(String payload, String fieldName) {
	String result = "---";
	if (payload != NULL && fieldName != NULL) {
		int starter = payload.indexOf("[" + fieldName + "[");
		if (starter >= 0) {
			int ender = payload.indexOf("]]", starter + 1);
			if (ender >= 0) {
				result = payload.substring(starter + fieldName.length() + 2, ender);
			}
		}
	}
	return result;
};

// helper string to display to both serial port and OLED - use only for initial startup or unusual (i.e. error) conditons, so you don't flash stuff to OLED in normal use
void displayAll(String text) {
	Serial.println(text);
	displayText(text);
}

#ifdef USE_TFT
String oldText = ""; // use this to prevent flashing by not updating if no text change
#endif //USE_TFT

// display text to OLED - every \n newline character advances to next line (4 lines available)
void displayText(String lines) {
#ifdef USE_OLED
	display.clear();
	display.setTextAlignment(TEXT_ALIGN_RIGHT);
	display.setFont(ArialMT_Plain_16);
#endif
#ifdef USE_TFT
	if (lines == oldText) {
		return;
	}
	oldText = lines;
//	tft.fillScreen(BACKGROUND_COLOR); 
	tft.fillRect(0, 0, tft.width(), FONT_HEIGHT*4, BACKGROUND_COLOR);
	tft.drawRect(0, 0, tft.width(), FONT_HEIGHT*4, TEXT_COLOR);
	tft.setTextSize(TEXT_SIZE);
	tft.setCursor(0, FONT_HEIGHT - FONT_OFFSET);
	tft.setTextColor(TOP_TEXT_COLOR); // first lines uses "special" color
#endif
	int lineY = 0;
	int ptr = 0;
	int len = lines.length();
	while (ptr < len) {
		int newLine = lines.indexOf("\n", ptr);
		if (newLine == -1) {
			newLine = len;
		}
		String line = lines.substring(ptr, newLine);
#ifdef USE_OLED
		if (line.length() > 0) {
			display.drawString(128, lineY, line);
		}
#endif
#ifdef USE_TFT
		tft.println(line);
		if (lineY == 0) {
			tft.setTextColor(TEXT_COLOR); // subsequent lines use normal color
		}
#endif
		lineY += 16;
		ptr = newLine + 1;
	}
#ifdef USE_OLED
	display.display();
#endif //USE_OLED
}

// updates just the bottom line of the display
void displayTextStatus(String line) {
#ifdef USE_OLED
	display.setColor(BLACK);
	display.fillRect(0, 16*3, 128, 16);
	display.setColor(WHITE);
	display.setTextAlignment(TEXT_ALIGN_RIGHT);
	display.setFont(ArialMT_Plain_16);
	display.drawString(128, 16*3, line);
	display.display();
#endif //USE_OLED
#ifdef USE_TFT
	tft.fillRect(0, tft.height() - FONT_HEIGHT, tft.width(), FONT_HEIGHT, BACKGROUND_COLOR);
	tft.drawRect(0, tft.height() - FONT_HEIGHT, tft.width(), FONT_HEIGHT, STATUS_COLOR);
	tft.setTextSize(TEXT_SIZE);
	tft.setCursor(0, tft.height() - FONT_OFFSET);
	tft.setTextColor(STATUS_COLOR);
	tft.println(line);
#endif //USE_TFT
}

// helper to convert a float value to a string with given number of decimals after period (TODO: round value to given decimal)
String floatToStr(float f, int decims) {
	float mult = 1.0;
	int l = decims;
	while (l > 0) {
		mult = mult * 10.0;
		l--;
	}
	int v = (f * mult);
	String res = String(v);
	if (decims != 0) {
		l = res.length();
		int p = l - decims;
		if (p <= 0) {
			res = "0." + res;
		} else {
			String intStr = res.substring(0, p);
			String decStr = res.substring(p);
			res = intStr + "." + decStr;
		}
	}
	return res;
}

//-------------------------------------------------------------------------------------------------------
void updateTemperature() {
	// read temperature sensors and update displayable strings
	unsigned long currentMillis = millis();
 
	if(currentMillis - previousTempMillis >= tempInterval) { // read only at given interval, not every time in the loop
		// save the last time you read the sensor 
		previousTempMillis = currentMillis;
		// Reading temperature for humidity takes about 250 milliseconds!
		// Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
		humidity = NAN;
		temp_f = NAN;
#if TEMP_TYPE == DS18
		DS18B20.requestTemperatures();
		temp_f = DS18B20.getTempFByIndex(0);
		humidity = 0.0;
		Serial.println("DS18 reads " + String(temp_f));
#else
		humidity = dht.readHumidity(); // Read humidity (percent)
		temp_f = dht.readTemperature(true); // Read temperature as Fahrenheit
#endif
		// Check if any reads failed and exit early (to try again).
		if (isnan(humidity) || isnan(temp_f)) {
			Serial.println("Failed to read from temperature sensor!");
			return;
		}
		tempStr = floatToStr(temp_f, 1);
#if TEMP_TYPE != DS18
		humStr = floatToStr(humidity, 1); // DHT type sensors also have humidity
#endif
	}
}

//-------------------------------------------------------------------------------------------------------
// helper to left zero pad a value to a given string size
String zeroPad(int value, int digits) {
	String s = String(value);
	while (s.length() < digits) {
		s = "0" + s;
	}
	return s;
}

// helper to check if we have an incoming HTTP request
String acceptRequest(WiFiClient client) {
	String result = "";
	unsigned long ms = 0;
	if (client) {
		// Wait until the client sends some data
		Serial.println("new client");
		unsigned long timeout = millis() + 1000;
		while(!client.available() && millis() < timeout){
			delay(1);
		}
		ms = millis();
		if (ms >= timeout) {
			Serial.println("Timed out in client wait...");
			client.flush();
		} else {
			// Read the first line of the request
			result = client.readStringUntil('\r');
			Serial.println(result);
			client.flush();
		}
	}
	return result;
}

// helper to make an outgoing HTTP GET request
String getHttpPayload(String url, unsigned long timeoutMaxMS) {
	attemptsCount++;
	String result = "";
	HTTPClient http;
	Serial.println("http trying: " + url);
	http.setTimeout(timeoutMaxMS);
	if (!http.begin(url)) {
		Serial.println("http failed");
		return result;
	}
	Serial.println("http connection made");
	int httpCode = http.GET();
	Serial.println("http connection status: " + httpCode);
	if(httpCode == HTTP_CODE_OK) {
		getsCount++;
		result = http.getString();
	} else {
		Serial.println("http connection FAILED");
	}
	http.end();
	return result;
}


// NPT time server stuff ********************************************************************************
const long timeInterval = 60*60*1000;		// interval at which to read time webpage (hourly)
unsigned long previousTimeMillis = timeInterval;		// will store last time was read

void updateTimeFromServer() {
	unsigned long currentMillis = millis();
	if(currentMillis - previousTimeMillis >= timeInterval) {
		// save the last time you read the server time 
		previousTimeMillis = currentMillis;
		nptAttempts++;
		if (setNTPtime() || firstTimeGot) {
			previousTimeMillis = currentMillis;
			firstTimeGot = true;
		} else {
			previousTimeMillis = currentMillis - timeInterval + (30*1000); // if failed, try again in 30 seconds
		}
	}
	dateStr = String(dayShortStr(weekday())) + ", " + String(monthShortStr(month())) + " " + String(day());
	String secStr = "";
#ifndef USE_TFT
	secStr = ":" + zeroPad(second(), 2); // add seconds in only if NOT tft because it causes too much flashing (need to figure out double buffering)
#endif
	timeStr = zeroPad(hourFormat12(), 2) + ":" + zeroPad(minute(), 2) + secStr + " " + (isAM() ? "AM" : "PM");
}

// NPT server time retrieve code -------------------------------------------------------------------------------------------------------
// Found at (and slightly munged) http://www.esp8266.com/viewtopic.php?p=18395 posted by user "nigelbe", it is all the info I have, thank you Nigel
// Note that I've modified the dst function to (hopefully) get correct daylight savings time offset for USA
#define localTimeOffset 21600UL // your localtime offset from UCT
WiFiUDP udp;
unsigned int localPort = 2390; // local port to listen for UDP packets
const char* timeServer = "us.pool.ntp.org"; // fall back to regional time server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
bool setNTPtime() {
	time_t epoch = 0UL;
	if((epoch = getFromNTP(timeServer)) != 0){ // get from time server
		epoch -= 2208988800UL + localTimeOffset;
		setTime(epoch += dstUSA(epoch));
		nptGets++;
		return true;
	}
	return false;
}

unsigned long getFromNTP(const char* server) {
	udp.begin(localPort);
	sendNTPpacket(server); // send an NTP packet to a time server
	delay(1000); // wait to see if a reply is available
	int cb = udp.parsePacket();
	if (!cb) {
		Serial.println("no packet yet");
		return 0UL;
	}
	Serial.print("packet received, length=");
	Serial.println(cb);
	// We've received a packet, read the data from it
	udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

	//the timestamp starts at byte 40 of the received packet and is four bytes,
	// or two words, long. First, extract the two words:

	unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
	unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
	// combine the four bytes (two words) into a long integer
	// this is NTP time (seconds since Jan 1 1900):
	udp.stop();
	return (unsigned long) highWord << 16 | lowWord;
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(const char* server) {
	Serial.print("sending NTP packet to ");
	Serial.println(server);
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011; // LI, Version, Mode
	packetBuffer[1] = 0; // Stratum, or type of clock
	packetBuffer[2] = 6; // Polling Interval
	packetBuffer[3] = 0xEC; // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;

	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	udp.beginPacket(server, 123); //NTP requests are to port 123
	udp.write(packetBuffer, NTP_PACKET_SIZE);
	udp.endPacket();
}

int dstUSA (time_t t) // calculate if summertime in USA (2nd Sunday in Mar, first Sunday in Nov)
{
	tmElements_t te;
	te.Year = year(t)-1970;
	te.Month = 3;
	te.Day = 1;
	te.Hour = 0;
	te.Minute = 0;
	te.Second = 0;
	time_t dstStart,dstEnd, current;
	dstStart = makeTime(te);
	dstStart = secondSunday(dstStart);
	dstStart += 2*SECS_PER_HOUR; //2AM
	te.Month=11;
	dstEnd = makeTime(te);
	dstEnd = firstSunday(dstEnd);
	dstEnd += SECS_PER_HOUR; //1AM
	if (t>=dstStart && t<dstEnd) {
		return (3600); //Add back in one hours worth of seconds - DST in effect
	} else {
		return (0);	//NonDST
	}
}

time_t secondSunday(time_t t)
{
	t = nextSunday(t); //Once, first Sunday
	return nextSunday(t); // second Sunday
}

time_t firstSunday(time_t t)
{
	return nextSunday(t); //Once, first Sunday
}


