/*  PurpleAir Display: Show current PM2.5 from a local PurpleAir on a 16x2 OLED display.
 *
 *  Connect to wifi, connect to local PurpleAir monitor, get the current PM2.5 values
 *  from the air monitor's JSON API, print the air quality on attached 16x2 OLED display.
 *
 *  !!--------------------------- HERE BE DRAGONS ---------------------------!!
 *  !!                                                                       !!
 *  !!        This program relies on delay() for timing which is bad.        !!
 *  !!                    Don't use this. Use version 2.0+                   !!
 *  !!=======================================================================!!
 *
 *  The LCD is wired to the top header of the HUZZAH32, sequentially (except for power):
 *    LCD 5V pin to HUZZAH pin USB
 *    LCD GND pin to HUZZAH pin GND
 *    LCD RS pin to HUZZAH pin 13
 *    LCD R/W pin to HUZZAH pin 12
 *    LCD Enable pin to HUZZAH pin 27
 *    LCD D4 pin to HUZZAH pin 33
 *    LCD D5 pin to HUZZAH pin 15
 *    LCD D6 pin to HUZZAH pin 32
 *    LCD D7 pin to HUZZAH pin 14
 */

#include <Adafruit_CharacterOLED.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#define DEBUG_STRINGS 0 // Enabling debug strings over serial increases program size

const char* ssid            = "Starbucks Wifi";
const char* password        = "psk here";
const char* api_server      = "192.168.10.10";
const char* url             = "/json";
const char* version         = "v1.0";

Adafruit_CharacterOLED lcd(OLED_V2, 13, 12, 27, 33, 15, 32, 14);
WiFiClient client;

void(* resetFunc) (void) = 0;

// Write both lines to the 16x2 display, existing contents cleared
void lcdLines(String l1, String l2)
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(l1);
    lcd.setCursor(0, 1);
    lcd.print(l2);
}

void setup()
{
    Serial.begin(115200);
    delay(10);

    #if DEBUG_STRINGS
      Serial.print("[*] PurpleAir Display ");
      Serial.println(version);
    #endif

    lcdLines("PurpleAir", String("Display     ") + version);
    delay(2500);
    
    // We start by connecting to a WiFi network
    #if DEBUG_STRINGS 
      Serial.println();
      Serial.println();
      Serial.print("[+] Connecting to ");
      Serial.println(ssid);
    #endif

    lcdLines("Connecting to:", ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        
        #if DEBUG_STRINGS
          Serial.print(".");
        #endif
    }
    
    #if DEBUG_STRINGS
      byte macAddr[6];
      Serial.println();
      Serial.println("[+] WiFi connected");
      Serial.print("[.] IP address: ");
      Serial.println(WiFi.localIP());
      
      WiFi.macAddress(macAddr);
      Serial.print("[.] MAC address: ");
      Serial.print(macAddr[5],HEX);
      Serial.print(":");
      Serial.print(macAddr[4],HEX);
      Serial.print(":");
      Serial.print(macAddr[3],HEX);
      Serial.print(":");
      Serial.print(macAddr[2],HEX);
      Serial.print(":");
      Serial.print(macAddr[1],HEX);
      Serial.print(":");
      Serial.println(macAddr[0],HEX);
    #endif
    
    lcdLines("Connected as:", WiFi.localIP().toString());
}

void loop()
{
    const int httpPort = 80;
    
    delay(1000);

    #if DEBUG_STRINGS
      Serial.print("[+] Connecting to: ");
      Serial.println(api_server);
    #endif
    
    // Use WiFiClient class to create TCP connections
    if (!client.connect(api_server, httpPort)) {
        #if DEBUG_STRINGS
          Serial.println("[x] Connection failed.");
        #endif
        
        lcdLines("Connect failed:", String(api_server)+":"+httpPort);
        delay(2500);
        if(WiFi.status() != WL_CONNECTED) {
            resetFunc();
        }
        return;
    }

    #if DEBUG_STRINGS
      Serial.print("[+] Requesting URL: ");
      Serial.println(url);
    #endif

    // This will send the request to the server
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + api_server + "\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
        if (millis() - timeout > 5000) {
            #if DEBUG_STRINGS
              Serial.println("[x] Client timeout.");
            #endif
            
            client.stop();
            lcdLines("Client timeout.", "");
            delay(2500);
            if(WiFi.status() != WL_CONNECTED) {
                resetFunc();
            }
            return;
        }
    }

    // Check HTTP status
    char status[32] = {0};
    client.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      #if DEBUG_STRINGS
        Serial.print("[x] Unexpected response: ");
        Serial.println(status);
      #endif
      
      lcdLines("Not an HTTP", "200 response");
      delay(2500);
      if(WiFi.status() != WL_CONNECTED) {
          resetFunc();
      }
      return;
    }
  
    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!client.find(endOfHeaders)) {
      #if DEBUG_STRINGS
        Serial.println("[x] Invalid response, couldn't find end of headers.");
      #endif
      
      lcdLines("Not an HTTP", "response");
      delay(2500);
      if(WiFi.status() != WL_CONNECTED) {
          resetFunc();
      }
      return;
    }
  
    // Allocate JSON doc, will hold response from API server
    DynamicJsonDocument jsonDoc(1700);
  
    // Parse JSON object
    DeserializationError jsonError = deserializeJson(jsonDoc, client);
    if (jsonError) {
      #if DEBUG_STRINGS
        Serial.println("[x] JSON parsing failed.");
        Serial.println(jsonError.c_str());
      #endif
      
      lcdLines("JSON parsing", "failed :(");
      delay(2500);
      if(WiFi.status() != WL_CONNECTED) {
          resetFunc();
      }
      return;
    }
  
    // Extract values  
    #if DEBUG_STRINGS
      String sensor = jsonDoc["Geo"];
      int rssi = jsonDoc["rssi"];
    #endif
    
    float pm25_a = jsonDoc["pm2_5_atm"];
    float pm25_b = jsonDoc["pm2_5_atm_b"];
    String pm25 = String((pm25_a + pm25_b) / 2.0);

    #if DEBUG_STRINGS
      Serial.println("[+] Response:");
      Serial.print("Name: ");
      Serial.println(sensor);
      Serial.print("Sensor RSSI: ");
      Serial.println(String(rssi));
      Serial.print("PM2.5 Channel A: ");
      Serial.println(String(pm25_a));
      Serial.print("PM2.5 Channel B: ");
      Serial.println(String(pm25_b));
      Serial.print("PM2.5 Average: ");
      Serial.println(pm25);
    #endif

    // Disconnect
    client.stop();

    lcdLines(String("PM2.5 ")+pm25, ""); //, WiFi.localIP());

    #if DEBUG_STRINGS
      Serial.println();
      Serial.println("[.] Done. Waiting 40 seconds to fetch again.");
    #endif

    // Print a . every 10 seconds on the right edge to indicate it's still working
    delay(10000);
    lcd.setCursor(13, 0);
    lcd.print(".");
    
    #if DEBUG_STRINGS
      Serial.print(".");
    #endif

    delay(10000);
    lcd.setCursor(14, 0);
    lcd.print(".");
    
    #if DEBUG_STRINGS
      Serial.print(".");
    #endif

    delay(10000);
    lcd.setCursor(15, 0);
    lcd.print(".");
    
    #if DEBUG_STRINGS
      Serial.println(".");
    #endif

    delay(10000);
}
