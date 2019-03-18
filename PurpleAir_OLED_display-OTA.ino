/*  PurpleAir Display: Show current PM2.5 from a local PurpleAir on a 16x2 OLED display.
 *
 *  Connect to wifi, connect to local PurpleAir monitor, get the current PM2.5 values
 *  from the air monitor's JSON API, print the air quality on attached 16x2 OLED display.
 *
 *  Features:
 *  - Hardware timers and interrupts for polling the API and updating the display
 *  - OTA supported from within Arduino IDE
 *  - Separate configuration file, config.h
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
// Firmware OTA
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "config.h"

const char* version = "v2.0";

// Internal variables
Adafruit_CharacterOLED lcd(OLED_V2, 13, 12, 27, 33, 15, 32, 14);
WiFiClient client;
hw_timer_t * apiTimer = NULL;
hw_timer_t * aliveTimer = NULL;
volatile SemaphoreHandle_t apiTimerSemaphore;
volatile SemaphoreHandle_t aliveTimerSemaphore;
portMUX_TYPE aliveTimerMux = portMUX_INITIALIZER_UNLOCKED;
volatile int aliveDotsCounter;

void IRAM_ATTR onApiTimer(){
    // Give a semaphore that we can check in main loop
    xSemaphoreGiveFromISR(apiTimerSemaphore, NULL);
}

void IRAM_ATTR onAliveTimer(){
    // Give a semaphore that we can check in main loop
    xSemaphoreGiveFromISR(aliveTimerSemaphore, NULL);
    // Increment the number of dots that should be displayed
    portENTER_CRITICAL_ISR(&aliveTimerMux);
    aliveDotsCounter++;
    portEXIT_CRITICAL_ISR(&aliveTimerMux);
}

void(* resetFunc) (void) = 0;

// Write both lines to the 16x2 display, existing contents cleared
void lcdLines(String top_line, String bottom_line)
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(top_line);
    lcd.setCursor(0, 1);
    lcd.print(bottom_line);
}

// Poll the PurpleAir device's JSON API
// Parsing the response and writing to the attached display
void pollApi()
{
    #if DEBUG_STRINGS
      Serial.print("[+] Connecting to: ");
      Serial.print(api_server);
      Serial.print(":");
      Serial.print(api_port);
      Serial.println(api_url);
    #endif

    // Use WiFiClient class to create TCP connections
    if (!client.connect(api_server, api_port)) {
        #if DEBUG_STRINGS
          Serial.println("[x] Connection failed.");
        #endif
        
        lcdLines("Connect failed:", String(api_server)+":"+api_port);
        delay(2500);
        if(WiFi.status() != WL_CONNECTED) {
            resetFunc();
        }
        return;
    }

    // This will send the request to the API server
    #if DEBUG_STRINGS
      Serial.print("[+] Requesting URL: ");
      Serial.println(api_url);
    #endif

    client.print(String("GET ") + api_url + " HTTP/1.1\r\n" +
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

    // Parse response: check HTTP status
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
 
    // Parse response: ignore all HTTP headers
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

    // Parse response: read and parse JSON body
    DynamicJsonDocument jsonDoc(1700);
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

    // Extract values from JSON object
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

    // Drop the HTTP connection
    client.stop();

    lcdLines(String("PM2.5 ")+pm25, ""); //, WiFi.localIP());

    #if DEBUG_STRINGS
      Serial.println();
      Serial.println("[.] Done. Waiting api_poll_seconds to fetch again.");
    #endif
}

// Runs once on boot
void setup()
{
    Serial.begin(115200);

    #if DEBUG_STRINGS
      Serial.print("[*] PurpleAir Display ");
      Serial.println(version);
    #endif

    // Create semaphore to inform us when the timer has fired
    apiTimerSemaphore = xSemaphoreCreateBinary();
    aliveTimerSemaphore = xSemaphoreCreateBinary();

    // Use 1st timer of 4 (counted from zero).
    // Set 80 clock divider (main clk is 80MHz) for prescaler
    // This gives us 1 timer tick per microsecond
    apiTimer = timerBegin(0, 80, true);
    aliveTimer = timerBegin(1, 80, true);

    // Call onTimer function when timer interrupts
    timerAttachInterrupt(apiTimer, &onApiTimer, true);
    timerAttachInterrupt(aliveTimer, &onAliveTimer, true);

    // Interrupt every 1000000 ticks (1 second in this case because 80MHz/prescaler = 1000000)
    timerAlarmWrite(apiTimer, 1000000 * poll_api_seconds, true);
    timerAlarmWrite(aliveTimer, 1000000 * 10, true);
  
    // Start the timers
    timerAlarmEnable(apiTimer);
    timerAlarmEnable(aliveTimer);
    
    lcdLines("PurpleAir    OTA", String("Display     ") + version);
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
    
      Serial.print("[.] Hostname: ");
      Serial.println(this_hostname);
    #endif
    
    lcdLines("Connected as:", WiFi.localIP().toString());

    // OTA
    ArduinoOTA.setHostname(this_hostname);

    #if OTA_IS_PASSWORD_PROTECTED
    #if PASSWORD_IS_MD5_HASH
      ArduinoOTA.setPasswordHash(ota_password);
    #else
      ArduinoOTA.setPassword(ota_password);
    #endif
    #endif

    ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
        
      #if DEBUG_STRINGS
        Serial.println("[+] Started updating " + type);
      #endif
    })
    #if DEBUG_STRINGS
      .onEnd([]() {
        Serial.println("\n[+] Done updating");
      })
    #endif
    .onProgress([](unsigned int progress, unsigned int total) {
      #if DEBUG_STRINGS
        Serial.printf("[.] Update progress: %u%%\r", (progress / (total / 100)));
      #endif
      
      lcd.setCursor(0, 1);
      lcd.printf("Updating %u%%", (progress / (total / 100)));
    })
    #if DEBUG_STRINGS
      .onError([](ota_error_t error) {
        Serial.printf("[!] Update error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      })
    #endif
    ;

    ArduinoOTA.begin();
    pollApi();
}

// Runs in an infinite loop after setup() finishes
void loop()
{        
    // Handle timers firing, usually just by calling the handler function
    if (xSemaphoreTake(apiTimerSemaphore, 0) == pdTRUE){
      #if DEBUG_STRINGS
        Serial.println("API polling timer fired");
      #endif

      pollApi();
    }

    if (xSemaphoreTake(aliveTimerSemaphore, 0) == pdTRUE){
      // Print a . to indicate everything's still working
      #if DEBUG_STRINGS
        Serial.println("Alive timer fired");
      #endif

      if (aliveDotsCounter >= 4) {
        // Reset the indicator, we only want 3 dots on screen at once
        portENTER_CRITICAL_ISR(&aliveTimerMux);
        aliveDotsCounter = 0;
        portEXIT_CRITICAL_ISR(&aliveTimerMux);

        lcd.setCursor(13, 0);
        lcd.print(" ");
        lcd.setCursor(14, 0);
        lcd.print(" ");
        lcd.setCursor(15, 0);
        lcd.print(" ");

        #if DEBUG_STRINGS
          Serial.println("Dots cleared");
        #endif
      } else {
        int cursorPosition = 12 + aliveDotsCounter;

        lcd.setCursor(cursorPosition, 0);
        lcd.print(".");

        #if DEBUG_STRINGS
          Serial.print(".");
        #endif
      }
    }

    // Always handle incoming OTA updates
    ArduinoOTA.handle();
}
