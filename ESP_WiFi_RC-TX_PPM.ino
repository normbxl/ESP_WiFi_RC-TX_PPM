/*
 Name:    PPM_WiFi_RC.ino
 Created: 21.10.2019 21:13:11
 Author:  norman
*/
#define ESP01

#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <ESP8266WiFiType.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiAP.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <string.h>
#include <stdint.h>

#include <ArduinoOTA.h>



#ifdef ESP01
#define println(s)
#else
#define println(s)  Serial.println(s)
#endif


#define PIN_S1        14


#define CHANNELS      8

#ifdef ESP01
#define PIN_PPM       2   // U1TX Pin
#define LED_ONBOARD   5 // should be GPIO 2 but that on is used, 5 is NC
#else
#define PIN_PPM       5
#define LED_ONBOARD   LED_BUILTIN
#endif

#define PPM_TIMING_MAX_US 1500
#define PPM_TIMING_Min_US 500
#define PPM_MARGIN_US   50

const String SSID_BASE = String("ESP_WiFi_RC-TX_PPM");
const String PWD = emptyString;

const String ALT_SSID = String("[ALTERNATIVE-AP]");
const String ALT_PWD = String("[ALTERNATIVE-AP-PASSWORD]");

const char* formatJsonCtrl = "{\"cmd\":\"set\", \"type\":\"control\", \"S1\": %i, \"S2\": %i, \"M1\": %i, \"M2\": %i}";
const char* jsonConfig = "{\"cmd\":\"set\", \"type\":\"config\", \"S1\": \"SERVO\", \"S2\": \"SERVO\"}";

volatile uint8_t cur_channel = 0;

uint16_t ppm_values[CHANNELS] = { 0 };
int16_t servo_values[CHANNELS] = { 0 };

volatile unsigned long    t_falling;
volatile byte ledState = LOW;

IPAddress rcIPAddress;
uint16_t rcPort;

WiFiUDP udp;
ESP8266WebServer server(80);

enum ctrlMap {
  Throttle,
  Yaw,
  Lift,
  Pitch
};

boolean connected = false;
boolean otaInProgress = false;

void sendUdpPacket();

inline void toggleLed() {
  ledState = ledState == LOW ? HIGH : LOW;
  digitalWrite(LED_ONBOARD, ledState);
}

ICACHE_RAM_ATTR void onPinChange_handler() {
  register unsigned long tDiff;

  // rising
  if (digitalRead(PIN_PPM)) {
    tDiff = micros() - t_falling;
    if (tDiff < PPM_TIMING_MAX_US + PPM_MARGIN_US && tDiff >= PPM_TIMING_Min_US - PPM_MARGIN_US && cur_channel < CHANNELS) {
      ppm_values[cur_channel] = tDiff;
      cur_channel++;
    }
    else if (tDiff > PPM_TIMING_MAX_US * 4) {
      cur_channel = 0;
    }
  }
  else {
    t_falling = micros();
  }
}

void httpHandleRoot() {
  int i;
  String result("<!DOCTYPE html><head><title>WIFi-RC PPM</title></head><body>"\
    "<table><thead><tr><th>Channel</th><th>Value</th></tr></thead><tbody>\n");
  for (i = 0; i < CHANNELS; i++) {
    result += "<tr><td>";
    result += i;
    result += "</td><td>";
    result += servo_values[i];
    result += "</td></tr>\n";
  }
  result += "</tbody></table>\n<p>Connected to ";
  result += rcIPAddress.toString();
  result += "</p></body></html>";
  server.send(200, "text/html", result);
}

void httpHandleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void onOTAStart() {
  detachInterrupt(PIN_PPM);
  memset(servo_values, 0, CHANNELS * sizeof(int16_t));
  sendUdpPacket();
  otaInProgress = true;
  println("OTA started.");
}

void onOTAProgress(unsigned int progress, unsigned int total) {
  println(progress);
}

void onOTAError(ota_error_t error) {
  println("OTA Error ");
  
  if (error == OTA_AUTH_ERROR) {
    println("Auth Failed");
  }
  else if (error == OTA_BEGIN_ERROR) {
    println("Begin Failed");
  }
  else if (error == OTA_CONNECT_ERROR) {
    println("Connect Failed");
  }
  else if (error == OTA_RECEIVE_ERROR) {
    println("Receive Failed");
  }
  else if (error == OTA_END_ERROR) {
    println("End Failed");
  }
  otaInProgress = false;
}

void onOTAEnd() {
  println("OTA end.");
  otaInProgress = false;
}


// the setup function runs once when you press reset or power the board
void setup() {

#ifndef ESP01
  Serial.begin(115200);
  pinMode(PIN_PPM, INPUT);
#endif

  println("WiFi RC PPM starting");

  pinMode(LED_ONBOARD, OUTPUT);

  attachInterrupt(PIN_PPM, onPinChange_handler, CHANGE);

  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, HIGH);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  server.on("/", httpHandleRoot);
  server.onNotFound(httpHandleNotFound);
  server.begin();

#ifndef ESP01
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname("wifi-PPM");

  //ArduinoOTA.setPassword("larifarie");

  ArduinoOTA.onStart(onOTAStart);
  ArduinoOTA.onProgress(onOTAProgress);
  ArduinoOTA.onError(onOTAError);
  ArduinoOTA.onEnd(onOTAEnd);
#endif
}

boolean scanAndConnect() {
  int n = WiFi.scanNetworks();
  int delayCount = 0;
  boolean altWifiPresent = false;
  boolean res = false;
  for (int i = 0; n > 0 && i < n; i++) {
    String ssid = WiFi.SSID(i);
    println(ssid);
    if (ssid.startsWith(SSID_BASE)) {
      println(" -connecting..");
      WiFi.begin(ssid, PWD);

      for (delayCount = 0; delayCount < 8 && WiFi.status() != WL_CONNECTED; delayCount++) {
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) {
        res = true;
        i = n;
        println("Connected to network" + ssid);
      }
    }
    else if (ssid == ALT_SSID) {
      altWifiPresent = true;
    }
  }
  if (!res && altWifiPresent) {
    println("Connecting to " + ALT_SSID);
    WiFi.begin(ALT_SSID, ALT_PWD);
    for (delayCount = 0; delayCount < 20 && WiFi.status() != WL_CONNECTED; delayCount++) {
      delay(500);
      toggleLed();
    }
    if (WiFi.status() == WL_CONNECTED) {
      res = true;
      println("Connected to network " + ALT_SSID);
    }
  }

  if (res) {
    println("Starting MDNS..");
    if (!MDNS.begin("esp8266")) {
      println("Error setting up MDNS responder!");
    }
    else {
      // Add service to MDNS-SD
      //MDNS.addService("wifi-rc", "udp", 4210);

      println("MDNS query for esp8266._wifi-rc._udp..");
      n = MDNS.queryService("wifi-rc", "udp"); // Send out query for wifi-rc udp services

      println("mDNS query done");
      if (n == 0) {
        println("no services found");
      }
      else {
        // use first found device
        rcIPAddress = MDNS.IP(0);
        rcPort = MDNS.port(0);
        println("Found WiFi-RC at " + rcIPAddress.toString());
        udp.begin(rcPort);
      }
    }
  }

  return res;
}


void sendUdpPacket() {
  char strBuffer[128] = { '\0' };
  udp.beginPacket(rcIPAddress, rcPort);

  sprintf(strBuffer, formatJsonCtrl,
    servo_values[Yaw],        // S1 
    servo_values[Pitch],      // S2
    (servo_values[Lift] + 127) >> 1,  // M1
    servo_values[Throttle]      // M2
  );
  udp.write(strBuffer);
  udp.endPacket();

}

void handleUdpGet() {
  char buffer[128];
  int len;
  int size = udp.parsePacket();

  if (size > 0) {
    len = udp.read(buffer, sizeof(buffer) - 1);
    String str = String(buffer);
    if (len > 0 && str.indexOf("ok") != -1) {
      toggleLed();
    }
  }
}

// the loop function runs over and over again until power down or reset
void loop() {
  uint8_t i;
  if (connected) {
    if (!otaInProgress && cur_channel == CHANNELS) {
      cur_channel = 0;
      for (i = 0; i < CHANNELS; i++) {
        servo_values[i] = (int16_t)map(ppm_values[i], PPM_TIMING_Min_US, PPM_TIMING_MAX_US, 0, 255) - 127;
      }
      
      sendUdpPacket();
      
      handleUdpGet();
      delay(80);
    }
#ifndef ESP01
    ArduinoOTA.handle();
#endif
  }
  else {
    if (scanAndConnect()) {
      connected = true;
#ifndef ESP01
      ArduinoOTA.begin(true);
#endif
    }
    else {
      println("Failed to connect..");
    }
  }
  server.handleClient();
}
