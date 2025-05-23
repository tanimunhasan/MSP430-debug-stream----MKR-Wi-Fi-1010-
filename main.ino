/*  Author: Sayed Tanimun Hasan
    Company:  
    Date: 23-05-2025
*/

#include <WiFiNINA.h>
#include <FlashStorage.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// MQTT Config

// const char* mqtt_server = "9729edce927e45bf95fc5e6e99e9f25b.s1.eu.hivemq.cloud:8883";  // Replace with your cluster host
// const int mqtt_port = 8883;  // TLS port
// const char* mqtt_username = "****";  // Set in HiveMQ Cloud
// const char* mqtt_password = "***";

const char* mqtt_topic = "logs/mkr";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Captive Portal Config
WiFiServer webServer(80);
WiFiUDP udp;
const int DNS_PORT = 53;
const char* AP_SSID = "MKR_SETUP";

bool wifiReinitialiseFlag = false;

// Stored Credentials
struct WiFiCredentials {
  char ssid[64];
  char password[64];
};
FlashStorage(wifiCredStore, WiFiCredentials);

// URL Decoder *** This is used because some character like @ - they count as 004 type value
String urlDecode(String str) {
  String decoded = "";
  char temp[] = "0x00";
  for (unsigned int i = 0; i < str.length(); i++) {
    if (str[i] == '%') {
      if (i + 2 < str.length()) {
        temp[2] = str[i + 1];
        temp[3] = str[i + 2];
        decoded += (char)strtol(temp, NULL, 16);
        i += 2;
      }
    } else if (str[i] == '+') {
      decoded += ' ';
    } else {
      decoded += str[i];
    }
  }
  return decoded;
}

void saveCredentials(String ssid, String pass) {
  WiFiCredentials creds;
  ssid.toCharArray(creds.ssid, sizeof(creds.ssid));
  pass.toCharArray(creds.password, sizeof(creds.password));
  wifiCredStore.write(creds);
  Serial.print("Saved SSID: "); Serial.println(creds.ssid);
}

// Captive Portal DNS
void handleDNSRequest() 
{
  if (udp.parsePacket()) 
  {
    byte buffer[512];
    udp.read(buffer, 512);
    buffer[2] = 0x81; buffer[3] = 0x80;
    buffer[7] = 0x01;
    int queryEnd = 12;
    while (buffer[queryEnd] != 0) queryEnd++;
    queryEnd += 5;
    buffer[queryEnd++] = 0xC0; buffer[queryEnd++] = 0x0C;
    buffer[queryEnd++] = 0x00; buffer[queryEnd++] = 0x01;
    buffer[queryEnd++] = 0x00; buffer[queryEnd++] = 0x01;
    buffer[queryEnd++] = 0x00; buffer[queryEnd++] = 0x00;
    buffer[queryEnd++] = 0x00; buffer[queryEnd++] = 0x3C;
    buffer[queryEnd++] = 0x00; buffer[queryEnd++] = 0x04;
    IPAddress ip = WiFi.localIP();
    for (int i = 0; i < 4; i++) buffer[queryEnd++] = ip[i];
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.write(buffer, queryEnd);
    udp.endPacket();
  }
}

void startCaptivePortal() 
{
  Serial.println("Starting Captive Portal...");
  WiFi.end();
  WiFi.beginAP(AP_SSID);
  delay(1000);
  webServer.begin();
  udp.begin(DNS_PORT);
  Serial.println("Connect to: Jellyfish");
  Serial.println("Open browser to: http://192.168.4.1");
}

void handleClient() {
  WiFiClient client = webServer.available();
  if (!client) return;

  String req = "", body = "";
  while (client.connected()) 
  {
    if (client.available()) 
    {
      char c = client.read();
      req += c;
      if (req.endsWith("\r\n\r\n")) 
      {
        if (req.indexOf("POST") >= 0) 
        {
          delay(10);
          while (client.available()) body += (char)client.read();
          int s = body.indexOf("ssid=");
          int p = body.indexOf("&pass=");
          if (s >= 0 && p > s) {
            String ssid = urlDecode(body.substring(s + 5, p));
            String pass = urlDecode(body.substring(p + 6));
            saveCredentials(ssid, pass);
            client.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n<html><body><h2>Saved. Connecting...</h2></body></html>");
            client.stop();
            wifiReinitialiseFlag = true;  //  Reconnect without reset
            return;
          }
        } else {
          client.println("HTTP/1.1 200 OK\nContent-Type: text/html\n\n");
          client.println("<html><body><h2>Wi-Fi Setup</h2>");
          client.println("<form method='POST'>SSID: <input name='ssid'><br>Password: <input name='pass' type='password'><br><input type='submit' value='Connect'></form></body></html>");
          client.stop();
          return;
        }
      }
    }
  }
}

void connectWiFi(const char* ssid, const char* pass) 
{
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries++ < 20) 
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) 
  {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
  } else 
  {
    Serial.println("\nFailed. Starting portal.");
    startCaptivePortal();
  }
}

void connectMQTT() 
{
  while (!client.connected()) 
  {
    Serial.print("Connecting to MQTT...");
    if (client.connect("MKRLoggerClient")) 
    {
      Serial.println("Connected!");
    } else 
    {
      Serial.print(" failed, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void setup() 
{
  Serial.begin(9600);
  Serial1.begin(9600);
  delay(1000);
  while (Serial1.available()) Serial1.read();  // Clean buffer

  WiFiCredentials creds = wifiCredStore.read();
  if (strlen(creds.ssid) == 0 || strlen(creds.password) == 0) 
  {
    startCaptivePortal();
  } else 
  {
    connectWiFi(creds.ssid, creds.password);
    if (WiFi.status() != WL_CONNECTED) startCaptivePortal();
  }
  client.setKeepAlive(30);  // 30 seconds // 10 seconds
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  handleDNSRequest();
  handleClient();

  if (wifiReinitialiseFlag) 
  {
    wifiReinitialiseFlag = false;
    delay(1000);
    WiFiCredentials creds = wifiCredStore.read();
    connectWiFi(creds.ssid, creds.password);
    client.setServer(mqtt_server, mqtt_port);

    Serial1.begin(9600);
    delay(1000);
    while (Serial1.available()) Serial1.read();  // Flush
  }

  if (WiFi.status() == WL_CONNECTED) 
  {
    if (!client.connected()) 
    {
      Serial.println("[MQTT] Disconnected. Trying to reconnect...");
      connectMQTT();  // reconnect if lost
    } else {
      client.loop();  // maintain connection
    }

    //  Non-blocking UART check
    static String buffer = "";
    while (Serial1.available()) 
    {
      char c = Serial1.read();
      if (c == '\n') 
      {
        buffer.trim();
        if (buffer.length() > 0) 
        {
          Serial.println(buffer);
          client.publish(mqtt_topic, buffer.c_str());
        }
        buffer = "";  // clear after publish
      } else 
      {
        buffer += c;
      }
    }
  }
}

