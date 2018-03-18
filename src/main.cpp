#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h> //https://github.com/esp8266/Arduino
#else
#include <WiFi.h>
#endif

// needed for library
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <FS.h>
#include <NtpClientLib.h> // To timestamp RFID scans we get Unix Time from NTP Server
#include <PubSubClient.h>
#include <TimeLib.h>

AsyncWebServer server(80);
DNSServer dns;

os_timer_t myTimer;
bool tickOccured;
bool relayStatus = false;
int relayPin = 12;
long interval = 5 * 60 * 1000;
bool reset = false;
int mode = 0;

const char *mqttDeviceId = "";
const char *mqttServer = "";
const char *mqttUser = "";
const char *mqttPassword = "";
int mqttPort = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

void status() {
  client.publish("outTopic", "hello world");
  if (reset == false) {
    client.publish("outTopic", "reset");
    reset = true;
  }
  client.publish("outTopic", String(mode).c_str());
  client.publish("outTopic", String(interval).c_str());
}

void relayOn() {
  digitalWrite(relayPin, HIGH);
  relayStatus = true;
  client.publish("status", "on");
}

void relayOff() {
  digitalWrite(relayPin, LOW);
  relayStatus = false;
  client.publish("status", "offS");
}

// start of timerCallback
void timerCallback(void *pArg) { tickOccured = true; } // End of timerCallback

void updateConfig(int m, long i = interval) {
  File f = SPIFFS.open("/status", "w");
  f.println(String(i).c_str());
  f.println(String(m).c_str());
  f.close();
}

void callback(char *topic, byte *message, unsigned int length) {

  if ((strcmp(topic, "pump/ping") == 0)) {
    status();
  } else if ((strcmp(topic, "pump/status") == 0) && (char)message[0] == '0') {
    mode = 1;
    updateConfig(mode);
    relayOff();
  } else if ((strcmp(topic, "pump/status") == 0) && (char)message[0] == '1') {
    mode = 2;
    updateConfig(mode);
    relayOn();
  } else if ((strcmp(topic, "pump/restart") == 0) && (char)message[0] == '1') {
    ESP.restart();
  } else if ((strcmp(topic, "pump/auto") == 0)) {
    message[length] = '\0';
    String s = String((char *)message);
    int secs = s.toInt();
    if (secs > 0) {
      interval = secs * 1000;
      relayOn();
      mode = 0;
      os_timer_arm(&myTimer, interval, true);
      updateConfig(mode, interval);
    }
  }

  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
  }
}

void reconnect() {
  Serial.print("Attempting MQTT connection...");

  if (client.connect(mqttDeviceId, mqttUser, mqttPassword)) {
    Serial.println("connected");
    status();
    client.subscribe("pump/#");
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
  }
}

void setup() {

  Serial.begin(115200);
  Serial.println("Turning On");

  bool result = SPIFFS.begin();

  // this opens the file "f.txt" in read-mode
  File f = SPIFFS.open("/status", "r");

  if (!f) {
    Serial.println("File doesn't exist yet. Creating it");
    f = SPIFFS.open("/status", "w");
    f.println(String(interval).c_str());
    f.println(String(mode).c_str());
  } else {
    while (f.available()) {
      String sinterval = f.readStringUntil('\n');
      String smode = f.readStringUntil('\n');
      mode = smode.toInt();
      interval = sinterval.toInt();
    }
  }
  f.close();

  AsyncWiFiManager wifiManager(&server, &dns);
  wifiManager.autoConnect("AutoConnectAP");
  Serial.println("connected...yeey :)");

  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);

  pinMode(relayPin, OUTPUT);

  tickOccured = false;

  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, interval, true);

  const char *ntpserver = "pool.ntp.org";

  NTP.begin(ntpserver, 1);
  NTP.setInterval(60 * 60); // Poll every x minutes
}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (tickOccured == true) {
    Serial.println("Tick Occurred");
    tickOccured = false;
    if (mode == 0) {
      if (relayStatus) {
        relayOff();
      } else {
        relayOn();
      }
    }
  }
  yield();
}
