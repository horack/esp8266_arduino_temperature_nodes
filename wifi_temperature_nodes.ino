// Both of these are included when you pick the Time library in Arduino IDE library manager
#include <Time.h>
#include <TimeLib.h>
// This comes from ESP8266 libs I get when I use http://arduino.esp8266.com/stable/package_esp8266com_index.json in Arduino Preferences for Additional Board Manager URLs. 
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// This is an ESP8266 specific SSD1306 library (which shows up as "ESP8266 OLED Driver for SSD1306 display" in Library Manager)
#include <SSD1306.h>
#include <SSD1306Ui.h>
// Include appropriate temperature sensor lib here
#include <DHT_U.h> // unified DHT libs ?
#include <DallasTemperature.h>
#include <OneWire.h>
#define DS18 18 // pseudo value for Dallas temperature sensors

// This program will assign static IPs to your module(s). In my case my internal IPs are configured by the wifi router as 192.168.1.XXX
// Note that you should make sure that the NODEMCU_NODES array (further down in code) does contain an entry for whatever value you set IP_LAST_OCTET to
// -------------------------------------------------------------------------------------------------------------------------------------------------
// NOTE: Always make sure you set up IP_LAST_OCTET correctly when you burn OTA (wirelessly), because if you're selecting one specific device over-the-air, but will be burning a different IP to it, it can cause you confusion,
//       especially if there is already another node online with that same IP.
// --------------------------------------------------------------------------------------------------------------------------------------------------
#define IP_LAST_OCTET 43 // This defined IP_LAST_OCTET determines what 'XXX' is in the above IP, which defines this node's full static IP

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
const char* ssid = AP_SSID;
const char* password = AP_PASSWORD;
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
	{true, 41, "Master LR", NULL}, // when building with IP_LAST_OCTET of 41, you're building the master, you can move the 'true' flag to another node, if you want
	{false, 40, "Mel", NULL},
	{false, 42, "Garage", NULL},
	{false, 43, "SW Room", NULL},
	{false, 44, "Cold Room", NULL},
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
time_t curTimeSec = 0;
const long tempInterval = 5000; // interval at which to read sensor
unsigned long previousTempMillis = tempInterval; // will store last temp was read
String timeStr = "NO-TIME-SET";
String dateStr = "NO-DATE-SET";
// the following 2 ints are just for diagnostic purposes to see how many times we tried to reach NPT and succeeded (and how far)
int nptGets = 0;
int nptAttempts = 0;

// Temperature sensor stuff -------------------------------------------------------------------------------------
#define TEMP_TYPE DS18 // set this to DS18 for Dallas 3 pin sensors, or DHT22 or DTH11 for the DHT 4 pin sensors
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

// OLED 128X64 display settings -------------------------------------------------------------------------------------
// Pin definitions for SPI OLED (I have not tested SPI OLED with the ESP8266, so you're on your own)
//#define OLED_RESET	D0	// RESET
//#define OLED_DC	D2	// Data/Command
//#define OLED_CS	D8	// Chip select
// Pin definitions for I2C OLED
#define OLED_SDA	D2	// pin 14
#define OLED_SDC	D4	// pin 12
#define OLED_ADDR	0x3C
 // Uncomment one of the following based on OLED interface type (SPI or I2C)
// SSD1306 display(true, OLED_RESET, OLED_DC, OLED_CS); // FOR SPI
SSD1306	display(OLED_ADDR, OLED_SDA, OLED_SDC);	// For I2C

// Master node stuff -------------------------------------------------------------------------------------
#define SLAVE_CHECK_INTERVAL_SEC 60 // interval at which to scan slaves, don't make it too fast, scanning nodes is slow especially if some nodes are not online and responding
time_t slaveCheckNext = 0;
int getsCount = 0;
int bytesCount = 0;
int attemptsCount = 0;

// SETUP function -------------------------------------------------------------------------------------
void setup() {
	Serial.begin(115200);
	delay(10);

	// display stuff
	display.init();
	display.flipScreenVertically(); // I flip display because of how I have the board in my setup, you may or may not need/want to flip
	
	displayAll("Booting");

	// Connect to WiFi network
	displayAll("Connecting SSID:\n" + String(ssid));
	WiFi.mode(WIFI_STA);
	WiFi.config(myIp, gateway, subnet, dns1, dns2);
	WiFi.begin(ssid, password);
	while (WiFi.waitForConnectResult() != WL_CONNECTED) {
		displayAll("Connection Failed! Rebooting...");
		delay(5000);
		ESP.restart();
	}
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
	curTimeSec = now();
	if (ThisNode->isMaster && slaveCheckNext <= curTimeSec) { // am I a master and is it time to collect data from slaves ?
		collectSlaveData();
	}
 
	// Check if a client has connected
	WiFiClient client = server.available();
	String request = acceptRequest(client);
	if (request.length() > 0) { // we have a request
		String payload = "";
		if (request.indexOf("/reset") >= 0) {
			ESP.restart();
		} else if (request.indexOf("/data") >= 0) {
			payload = "[[temperature[" + tempStr + "]][[humidity[" + humStr + "]][name[" + nodeName + "]][time[" + timeStr + "]][date[" + dateStr + "]][uptime[" + (millis() / 1000) + "]]";
		} else {
			int textPtr = request.indexOf("text=");
			Serial.println("textPtr = " + textPtr);
			if (textPtr >= 0) {
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
				allNodes = "<br/>------other nodes-------<br/>";
				for (int i = 0; i < NODEMCU_NODE_COUNT; i++) {
					codeus::NODEMCU_NODE* node = &NODEMCU_NODES[i];
					//TODO: build response from all nodes
					if (node->result != NULL) {
						codeus::SLAVE_RESULT* res = node->result;
						allNodes = allNodes +
							"<font size='10' color='gold'><b>" + res->name + "</b></font>&nbsp;&nbsp;&nbsp;&nbsp;"  +
							"<font size='10' color='red'>" + res->temperature + "&deg;</font>&nbsp;&nbsp;&nbsp;&nbsp;" +
							"<font size='10' color='green'>" + res->humidity +"% rh</font>&nbsp;&nbsp;&nbsp;&nbsp;" +
							"<br>";
					}
				}
			}

			payload =
				"<!DOCTYPE HTML><html>"
				"<br><br>"
				"<font size='16' color='gold'><b>" + nodeName + "</b></font><br/>"
				"<font size='16' color='red'>" + tempStr + "&deg;</font>&nbsp;&nbsp;&nbsp;&nbsp;"
				"<font size='16' color='green'>" + humStr +"% rh</font>"
				"<br>"
				"Time: <font size='16' color='black'>" + timeStr + "</font><br>"
				"Date: <font size='16' color='black'>" + dateStr + "</font><br>"
				"<form action='/' method='get'><input type='text' name='text'/><input type='submit' value='Send'/></form>"
				"<br/>NPT attempts: " + nptAttempts + " NPT succeses: " + nptGets + " Uptime seconds: " + (millis() / 1000) + " at: " + attemptsCount + " gc: " + getsCount +
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
		Serial.println("Client disonnected");
		Serial.println("");
	} // end client check
	
	// display stuff
	displayText(
		tempStr + "F" + "  " + "h:" + humStr + "%" + "\n" +
		nodeName + "\n" +
		dateStr + "\n" +
		timeStr + " \n" +
		""
	);
}

void collectSlaveData() {
	slaveCheckNext = curTimeSec + SLAVE_CHECK_INTERVAL_SEC;
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
				node->result = new codeus::SLAVE_RESULT {curTimeSec, n, t, h}; // maybe use now() instead of curTimeSec
			}
		}
	}		
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

// display text to OLED - every \n newline character advances to next line (4 lines available)
void displayText(String lines) {
	display.clear();
	display.setTextAlignment(TEXT_ALIGN_RIGHT);
	display.setFont(ArialMT_Plain_16);
	int lineY = 0;
	int ptr = 0;
	int len = lines.length();
	while (ptr < len) {
		int newLine = lines.indexOf("\n", ptr);
		if (newLine == -1) {
			newLine = len;
		}
		String line = lines.substring(ptr, newLine);
		if (line.length() > 0) {
			display.drawString(128, lineY, line);
		}
		ptr = newLine + 1;
		lineY += 16;
	}
	display.display();
}

// updates just the bottom line of the display
void displayTextStatus(String line) {
	display.setTextAlignment(TEXT_ALIGN_RIGHT);
	display.setFont(ArialMT_Plain_16);
	display.setColor(BLACK);
	display.fillRect(0, 16*3, 128, 16);
	display.setColor(WHITE);
	display.drawString(128, 16*3, line);
	display.display();
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
	// Wait at least 2 seconds seconds between measurements.
	// if the difference between the current time and last time you read
	// the sensor is bigger than the interval you set, read the sensor
	// Works better than delay for things happening elsewhere also
	unsigned long currentMillis = millis();
 
	if(currentMillis - previousTempMillis >= tempInterval) {
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
		humStr = floatToStr(humidity, 1);
#endif
	}
}

//-------------------------------------------------------------------------------------------------------
const long timeInterval = 60*60*1000;		// interval at which to read time webpage (hourly)
unsigned long previousTimeMillis = timeInterval;		// will store last time was read
String zeroPad(int value, int digits) {
	String s = String(value);
	while (s.length() < digits) {
		s = "0" + s;
	}
	return s;
}

// checks if we have an incoming HTTP request
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

// make an outgoing HTTP GET request
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
			previousTimeMillis = currentMillis - timeInterval + (1*60*1000); // if failed, try again in 1 minute
		}
	}
	dateStr = String(dayShortStr(weekday())) + ", " + String(monthShortStr(month())) + " " + String(day());
	timeStr = zeroPad(hourFormat12(), 2) + ":" + zeroPad(minute(), 2) + ":" + zeroPad(second(), 2) + " " + (isAM() ? "AM" : "PM");
}

//-------------------------------------------------------------------------------------------------------
#define localTimeOffset 21600UL // your localtime offset from UCT
WiFiUDP udp;
unsigned int localPort = 2390; // local port to listen for UDP packets
const char* timeServer = "us.pool.ntp.org"; // fall back to regional time server
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
bool setNTPtime()
{
	time_t epoch = 0UL;
	if((epoch = getFromNTP(timeServer)) != 0){ // get from time server
		epoch -= 2208988800UL + localTimeOffset;
		setTime(epoch += dst(epoch));
		nptGets++;
		return true;
	}
	return false;
}
unsigned long getFromNTP(const char* server)
{
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
unsigned long sendNTPpacket(const char* server)
{
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

int dst (time_t t) // calculate if summertime in USA (2nd Sunday in Mar, first Sunday in Nov)
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


