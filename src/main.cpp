#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <FastLED.h>

// Sensor configuration
#define PM_SENSOR_ID "85707"    // Particulate matter sensor
#define TEMP_SENSOR_ID "85708"  // Temperature sensor
#define API_HOST "data.sensor.community"
#define API_PATH "/airrohr/v1/sensor/"
#define MAX_RETRIES 3
#define RETRY_DELAY 1000  // 1 second between retries
#define HTTP_TIMEOUT 10000 // 10 seconds timeout

// LED Matrix configuration
#define LED_PIN     4
#define NUM_LEDS    256
#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 8
#define BRIGHTNESS  40
CRGB leds[NUM_LEDS];

bool initialFetchDone = false;  // Flag to track initial fetch

// Global variables for Sensordata
char PM25_actual[10];
char TEMP_actual[10];
char ALTITUDE[10];
char localTime[20];

// Global variables for Weather data
float currentTemp = 0;        // Current temperature from OpenMeteo
float currentPressure = 0;    // Current pressure in hPa
int currentWeatherCode = 0;   // Current weather code
float tomorrowMinTemp = 0;    // Tomorrow's min temperature
float tomorrowMaxTemp = 0;    // Tomorrow's max temperature
int tomorrowWeatherCode = 0;  // Tomorrow's weather code

// OpenMeteo API endpoint for Chiang Mai
const char* weatherHost = "api.open-meteo.com";
const char* weatherPath = "/v1/forecast?latitude=18.7883&longitude=98.9853"
                      "&current=temperature_2m,weathercode,pressure_msl"
                      "&daily=weathercode,temperature_2m_max,temperature_2m_min"
                      "&timezone=Asia%2FBangkok"
                      "&forecast_days=2";

// Global variables for Display
char scrollText[100];
int scrollPosition = 0;


// Weather codes mapping
String getWeatherDescription(int code) {
    switch(code) {
        case 0: return "Clear";
        case 1: case 2: case 3: return "Cloudy";
        case 45: case 48: return "Foggy";
        case 51: case 53: case 55: return "Drizzle";
        case 61: case 63: case 65: return "Rain";
        case 80: case 81: case 82: return "Rain";
        default: return "Unknown";
    }
}

// Function to fetch weather data from OpenMeteo
void fetchWeatherData() {
    Serial.println("Fetching weather data...");
    WiFiClient client;

    if (!client.connect(weatherHost, 80)) {
        Serial.println("Connection to weather server failed!");
        return;
    }

    // Make HTTP request
    String request = String("GET ") + weatherPath + " HTTP/1.1\r\n" +
                    "Host: " + weatherHost + "\r\n" +
                    "Connection: close\r\n\r\n";
    client.print(request);

    // Wait for response with timeout
    unsigned long timeout = millis();
    while (!client.available()) {
        if (millis() - timeout > 5000) {
            Serial.println("Response timeout!");
            client.stop();
            return;
        }
        delay(100);
    }

    // Skip headers and read response
    String line;
    String jsonResponse;
    bool foundEndOfHeaders = false;
    
    while (client.available()) {
        line = client.readStringUntil('\n');
        if (!foundEndOfHeaders) {
            if (line.length() == 1 && line[0] == '\r') {
                foundEndOfHeaders = true;
            }
        } else {
            if (line.length() > 0 && isHexadecimalDigit(line[0])) {
                continue;
            }
            jsonResponse += line;
        }
    }
    
    jsonResponse.trim();
    if (!jsonResponse.startsWith("{")) {
        Serial.println("Invalid JSON response");
        return;
    }

    // Parse JSON response
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);

    if (error) {
        Serial.print("JSON parsing error: ");
        Serial.println(error.c_str());
        return;
    }

    // Extract weather data
    currentTemp = doc["current"]["temperature_2m"].as<float>();
    currentPressure = doc["current"]["pressure_msl"].as<float>();
    currentWeatherCode = doc["current"]["weathercode"].as<int>();
    
    tomorrowMaxTemp = doc["daily"]["temperature_2m_max"][1].as<float>();
    tomorrowMinTemp = doc["daily"]["temperature_2m_min"][1].as<float>();
    tomorrowWeatherCode = doc["daily"]["weathercode"][1].as<int>();

    client.stop();
}

// Function to convert UTC timestamp to Thailand time (UTC+7)
String adjustToLocalTime(const char* timestamp) {
    // Parse timestamp (format: "2025-02-16 03:11:56")
    int year, month, day, hour, minute, second;
    sscanf(timestamp, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    
    // Add 7 hours for Thailand timezone
    hour += 7;
    
    // Handle day changes
    if (hour >= 24) {
        hour -= 24;
        day += 1;
    }
    
    // Format the adjusted time
    // char localTime[20];
    sprintf(localTime, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
    return String(localTime);
}

// Basic 5x7 font data for Latin characters
const uint8_t PROGMEM FONT_5X7[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, // ! 33
    0x00, 0x07, 0x00, 0x07, 0x00, // " 34
    0x14, 0x7F, 0x14, 0x7F, 0x14, // # 35
    0x24, 0x2A, 0x7F, 0x2A, 0x12, // $ 36
    0x23, 0x13, 0x08, 0x64, 0x62, // % 37
    0x36, 0x49, 0x55, 0x22, 0x50, // & 38
    0x00, 0x05, 0x03, 0x00, 0x00, // ' 39
    0x00, 0x1C, 0x22, 0x41, 0x00, // ( 40
    0x00, 0x41, 0x22, 0x1C, 0x00, // ) 41
    0x14, 0x08, 0x3E, 0x08, 0x14, // * 42
    0x08, 0x08, 0x3E, 0x08, 0x08, // + 43
    0x00, 0x50, 0x30, 0x00, 0x00, // , 44
    0x08, 0x08, 0x08, 0x08, 0x08, // - 45
    0x00, 0x60, 0x60, 0x00, 0x00, // . 46
    0x20, 0x10, 0x08, 0x04, 0x02, // / 47
    0x3E, 0x51, 0x49, 0x45, 0x3E, // 0 48
    0x00, 0x42, 0x7F, 0x40, 0x00, // 1 49
    0x42, 0x61, 0x51, 0x49, 0x46, // 2 50
    0x21, 0x41, 0x45, 0x4B, 0x31, // 3 51
    0x18, 0x14, 0x12, 0x7F, 0x10, // 4 52
    0x27, 0x45, 0x45, 0x45, 0x39, // 5 53
    0x3C, 0x4A, 0x49, 0x49, 0x30, // 6 54
    0x01, 0x71, 0x09, 0x05, 0x03, // 7 55
    0x36, 0x49, 0x49, 0x49, 0x36, // 8 56
    0x06, 0x49, 0x49, 0x29, 0x1E, // 9 57
    0x00, 0x36, 0x36, 0x00, 0x00, // : 58
    0x00, 0x56, 0x36, 0x00, 0x00, // ; 59
    0x08, 0x14, 0x22, 0x41, 0x00, // < 60
    0x14, 0x14, 0x14, 0x14, 0x14, // = 61
    0x00, 0x41, 0x22, 0x14, 0x08, // > 62
    0x02, 0x01, 0x51, 0x09, 0x06, // ? 63
    0x32, 0x49, 0x79, 0x41, 0x3E, // @ 64
    0x7E, 0x11, 0x11, 0x11, 0x7E, // A 65
    0x7F, 0x49, 0x49, 0x49, 0x36, // B 66
    0x3E, 0x41, 0x41, 0x41, 0x22, // C 67
    0x7F, 0x41, 0x41, 0x22, 0x1C, // D 68
    0x7F, 0x49, 0x49, 0x49, 0x41, // E 69
    0x7F, 0x09, 0x09, 0x09, 0x01, // F 70
    0x3E, 0x41, 0x49, 0x49, 0x7A, // G 71
    0x7F, 0x08, 0x08, 0x08, 0x7F, // H 72
    0x00, 0x41, 0x7F, 0x41, 0x00, // I 73
    0x20, 0x40, 0x41, 0x3F, 0x01, // J 74
    0x7F, 0x08, 0x14, 0x22, 0x41, // K 75
    0x7F, 0x40, 0x40, 0x40, 0x40, // L 76
    0x7F, 0x02, 0x0C, 0x02, 0x7F, // M 77
    0x7F, 0x04, 0x08, 0x10, 0x7F, // N 78
    0x3E, 0x41, 0x41, 0x41, 0x3E, // O 79
    0x7F, 0x09, 0x09, 0x09, 0x06, // P 80
    0x3E, 0x41, 0x51, 0x21, 0x5E, // Q 81
    0x7F, 0x09, 0x19, 0x29, 0x46, // R 82
    0x46, 0x49, 0x49, 0x49, 0x31, // S 83
    0x01, 0x01, 0x7F, 0x01, 0x01, // T 84
    0x3F, 0x40, 0x40, 0x40, 0x3F, // U 85
    0x1F, 0x20, 0x40, 0x20, 0x1F, // V 86
    0x3F, 0x40, 0x38, 0x40, 0x3F, // W 87
    0x63, 0x14, 0x08, 0x14, 0x63, // X 88
    0x07, 0x08, 0x70, 0x08, 0x07, // Y 89
    0x61, 0x51, 0x49, 0x45, 0x43, // Z 90
    0x00, 0x7F, 0x41, 0x41, 0x00, // [ 91
    0x02, 0x04, 0x08, 0x10, 0x20, // \ 92
    0x00, 0x41, 0x41, 0x7F, 0x00, // ] 93
    0x04, 0x02, 0x01, 0x02, 0x04, // ^ 94
    0x40, 0x40, 0x40, 0x40, 0x40, // _ 95
    0x00, 0x01, 0x02, 0x04, 0x00, // ` 96
    0x20, 0x54, 0x54, 0x54, 0x78, // a 97
    0x7F, 0x48, 0x44, 0x44, 0x38, // b 98
    0x38, 0x44, 0x44, 0x44, 0x20, // c 99
    0x38, 0x44, 0x44, 0x48, 0x7F, // d 100
    0x38, 0x54, 0x54, 0x54, 0x18, // e 101
    0x08, 0x7E, 0x09, 0x01, 0x02, // f 102
    0x0C, 0x52, 0x52, 0x52, 0x3E, // g 103
    0x7F, 0x08, 0x04, 0x04, 0x78, // h 104
    0x00, 0x44, 0x7D, 0x40, 0x00, // i 105
    0x20, 0x40, 0x44, 0x3D, 0x00, // j 106
    0x7F, 0x10, 0x28, 0x44, 0x00, // k 107
    0x00, 0x41, 0x7F, 0x40, 0x00, // l 108
    0x7C, 0x04, 0x18, 0x04, 0x78, // m 109
    0x7C, 0x08, 0x04, 0x04, 0x78, // n 110
    0x38, 0x44, 0x44, 0x44, 0x38, // o 111
    0x7C, 0x14, 0x14, 0x14, 0x08, // p 112
    0x08, 0x14, 0x14, 0x18, 0x7C, // q 113
    0x7C, 0x08, 0x04, 0x04, 0x08, // r 114
    0x48, 0x54, 0x54, 0x54, 0x20, // s 115
    0x04, 0x3F, 0x44, 0x40, 0x20, // t 116
    0x3C, 0x40, 0x40, 0x20, 0x7C, // u 117
    0x1C, 0x20, 0x40, 0x20, 0x1C, // v 118
    0x3C, 0x40, 0x30, 0x40, 0x3C, // w 119
    0x44, 0x28, 0x10, 0x28, 0x44, // x 120
    0x0C, 0x50, 0x50, 0x50, 0x3C, // y 121
    0x44, 0x64, 0x54, 0x4C, 0x44, // z 122
    0x06, 0x09, 0x09, 0x06, 0x00  // degree symbol (°) (123)
};

// Helper function to convert x,y coordinates to LED index
uint16_t XY(uint8_t x, uint8_t y) {
    uint16_t i;
    
    if(x & 0x01) {
        // Odd rows run backwards
        uint8_t reverseY = (MATRIX_HEIGHT - 1) - y;
        i = (x * MATRIX_HEIGHT) + reverseY;
    } else {
        // Even rows run forwards
        i = (x * MATRIX_HEIGHT) + y;
    }
    
    return i;
}


// Function to test the display
void testDisplay() {
    //Serial.println("Running display test...");
    for (int i = 0; i < 3; i++) {
        // Turn all LEDs red
        fill_solid(leds, NUM_LEDS, CRGB::Yellow);
        FastLED.show();
        delay(50);
        
        // Turn all LEDs off
        FastLED.clear(true);
        delay(50);
    }

    FastLED.show();
    delay(100);
    
    Serial.println("Display test complete.");
}

// Function to set a pixel in the matrix
void setMatrixPixel(int x, int y, CRGB color) {
    if (x >= 0 && x < MATRIX_WIDTH && y >= 0 && y < MATRIX_HEIGHT) {
        // Convert x,y to LED index
        int index;
        if (x % 2 == 0) {
            // Even columns go up
            index = x * MATRIX_HEIGHT + y;
        } else {
            // Odd columns go down
            index = x * MATRIX_HEIGHT + (MATRIX_HEIGHT - 1 - y);
        }
        leds[index] = color;
    }
}

// Function to fetch sensor data
void fetchSensorData(const char* sensorId, bool isPMSensor) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://" + String(API_HOST) + String(API_PATH) + String(sensorId) + "/";
    
    if (isPMSensor) {
      Serial.print("Fetching PM2.5 data");
    } else {
      Serial.print("Fetching Temperature data");
    }
    
    // Set timeout to 10 seconds
    http.setTimeout(HTTP_TIMEOUT);
    
    // Retry mechanism
    int retries = 0;
    int httpResponseCode;
    
    while (retries < MAX_RETRIES) {
      if (retries > 0) {
        Serial.print("\nRetry #");
        Serial.print(retries);
        Serial.print(" of ");
        Serial.print(MAX_RETRIES - 1);
        Serial.print("... ");
        delay(RETRY_DELAY);
      }
      
      http.begin(url);
      httpResponseCode = http.GET();
      
      if (httpResponseCode > 0) {
        Serial.println(" done!");
        String payload = http.getString();
        
        // Allocate JsonDocument
        DynamicJsonDocument doc(8192);
        
        // Parse JSON
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
          Serial.print("deserializeJson() failed: ");
          Serial.println(error.c_str());
          return;
        }
        
        // Get the first (most recent) reading
        JsonObject firstReading = doc[0];
        if (!firstReading.isNull()) {
          JsonArray sensorValues = firstReading["sensordatavalues"];
          const char* timestamp = firstReading["timestamp"];
          
          // Get location data
          JsonObject location = firstReading["location"];
          if (!location.isNull()) {
              String localTime = adjustToLocalTime(timestamp);
              
              // Print location info only on initial fetch
              if (isPMSensor && !initialFetchDone) {
                  Serial.println("----------------------------------------");
                  Serial.println("Location: Pa Rang Cafe & Art Stay");
                  Serial.println("Chiang Mai, Thailand");
                  Serial.print("GPS: ");
                  Serial.print(location["latitude"].as<float>(), 6);
                  Serial.print(", ");
                  Serial.print(location["longitude"].as<float>(), 6);
                  Serial.print(" (Alt: ");
                  Serial.print(location["altitude"].as<float>());
                  Serial.println("m)");
                  Serial.println("Timezone: UTC+7 (Indochina Time)");
                  Serial.println("----------------------------------------");
                  //ALTITUDE = location["altitude"].as<float>();
                  sprintf(ALTITUDE, "%.1f", location["altitude"].as<float>());
                  initialFetchDone = true;  // Mark initial fetch as done
              }
              
              // Only print the values we're interested in
              for (JsonObject value : sensorValues) {
                  const char* value_type = value["value_type"];
                  const char* value_str = value["value"];
                  
                  if (isPMSensor) {
                      if (strcmp(value_type, "P2") == 0) {
                          Serial.println("----------------------------------------");
                          Serial.print(localTime);
                          Serial.print(" | PM2.5: ");
                          Serial.print(value_str);
                          Serial.println(" µg/m³");
                          Serial.println("----------------------------------------");
                          // was: PM25_actual = value_str;
                          strcpy(PM25_actual, value_str);
                          break;
                      }
                  } else {
                      if (strcmp(value_type, "temperature") == 0) {
                          Serial.println("----------------------------------------");
                          Serial.print(localTime);
                          Serial.print(" | Temperature: ");
                          Serial.print(value_str);
                          Serial.println(" °C");
                          Serial.println("----------------------------------------");
                          // was: TEMP_actual = value_str;
                          strcpy(TEMP_actual, value_str);
                          break;
                      }
                  }
              }
          }
        }
        break;  // Success, exit retry loop
      } else {
        // Print specific error based on code
        Serial.print("\nError: ");
        switch (httpResponseCode) {
          case -1: Serial.println("Connection failed"); break;
          case -2: Serial.println("Server not found"); break;
          case -3: Serial.println("Connection timed out"); break;
          case -4: Serial.println("Connection lost"); break;
          case -5: Serial.println("No or invalid response"); break;
          case -6: Serial.println("Invalid response length"); break;
          case -7: Serial.println("Connection refused"); break;
          case -8: Serial.println("Invalid request"); break;
          case -9: Serial.println("Client timeout"); break;
          case -10: Serial.println("Invalid response"); break;
          case -11: Serial.println("Connection reset"); break;
          default: Serial.println("Unknown error"); break;
        }
      }
      
      http.end();
      retries++;
    }
  }
}

// Function to draw a character
void drawChar(char c, int xOffset, CRGB color) {
    // Convert ASCII to font array index
    int charIndex = ((int)(c - 0x20)) * 5;  // Each character is 5 bytes
    if (charIndex < 0 || charIndex >= sizeof(FONT_5X7)) return;
    
    // Draw the character pixel by pixel
    for (int x = 0; x < 5; x++) {
        uint8_t line = FONT_5X7[charIndex + x];
        for (int y = 0; y < 7; y++) {
            if (xOffset + x >= 0 && xOffset + x < MATRIX_WIDTH) {
                if (line & (1 << y)) {
                    setMatrixPixel(xOffset + x, y, color);
                } else {
                    setMatrixPixel(xOffset + x, y, CRGB::Black);
                }
            }
        }
    }
}

void scrollMessage(const char* message, CRGB color) {
    Serial.print("Scrolling message: ");
    Serial.println(message);
    
    int textLength = strlen(message);
    int totalWidth = textLength * 6; // 5 pixels per char + 1 space
    
    for (int xStart = MATRIX_WIDTH; xStart > -totalWidth; xStart--) {
        FastLED.clear();
        int xPos = xStart;
        for (int i = 0; i < textLength; i++) {
            drawChar(message[i], xPos, color);
            xPos += 6;
        }
        FastLED.show();
        delay(20);  // Adjust scroll speed
    }
}

void updateDisplay() {
    // For testing, just rotate the colors
    CRGB temp = leds[0];
    for(int i = 0; i < NUM_LEDS-1; i++) {
        leds[i] = leds[i+1];
    }
    leds[NUM_LEDS-1] = temp;
    FastLED.show();
    delay(50);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

 // Initialize FastLED
  Serial.println("Initializing display...");
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
  testDisplay();
    
  Serial.println("\n----------------------------------------");
  Serial.println("Air Quality Monitor");
  Serial.println("----------------------------------------");
  Serial.print("Connecting to WiFi network: ");
  Serial.println(WIFI_SSID);
  scrollMessage("..initializing wifi..", CRGB::Blue);    // Check WiFi module status
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected successfully!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("----------------------------------------");

  char message[100];
  snprintf(message, sizeof(message), "connected to %s - IP: %s", WIFI_SSID, WiFi.localIP().toString().c_str());
  scrollMessage(message, CRGB::Green);
  
  // Reset the initial fetch flag when starting up
  initialFetchDone = false;
  
  // Fetch initial data from both sensors
  Serial.println("\nInitial data fetch:");
  fetchSensorData(PM_SENSOR_ID, true);
  delay(1000);
  fetchSensorData(TEMP_SENSOR_ID, false);
  Serial.println("\nNext update in 60 seconds...");
}

unsigned long lastFetchTime = 0;
const unsigned long fetchInterval = 60000; // Fetch every 60 seconds
unsigned long dotTimer = 0;
const unsigned long dotInterval = 5000; // Print dot every 5 seconds

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n----------------------------------------");
    Serial.println("WiFi connection lost!");
    scrollMessage("wifi disconnected", CRGB::Red);
    Serial.print("Reconnecting to: ");
    Serial.println(WIFI_SSID);
    
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nReconnected successfully!");
    Serial.println("----------------------------------------");
    scrollMessage("wifi reconnected", CRGB::Green);
    }
  

  // Print dots while waiting
  unsigned long currentTime = millis();
  if (currentTime - dotTimer >= dotInterval) {
    Serial.print(".");
    dotTimer = currentTime;
  }

  // Fetch data every minute
  if (currentTime - lastFetchTime >= fetchInterval) {
    Serial.println("\n\nFetching new data:");
    scrollMessage("..fetching data..", CRGB::White);

    fetchSensorData(PM_SENSOR_ID, true);
    delay(1000);
    fetchSensorData(TEMP_SENSOR_ID, false);
    delay(1000);
    fetchWeatherData();
    Serial.println("\nNext update in 60 seconds...");
    lastFetchTime = currentTime;
    dotTimer = currentTime; // Reset dot timer after fetch
  }
  
  // Original message (commented out)
  /*
  char combinedMsg[200];   
  snprintf(combinedMsg, sizeof(combinedMsg), 
           "Pa Rang Cafe & Art Stay (Alt: %s m), %s, PM 2.5: %s, ug/m, Temp: %s C",
           ALTITUDE, localTime, PM25_actual, TEMP_actual);
  */

  // New expanded message with weather data
  char combinedMsg[300];   
  snprintf(combinedMsg, sizeof(combinedMsg), 
           "Pa Rang Cafe (Alt: %sm) * %s * PM2.5: %s ug/m, Temp: %s%cC * Now: %s, %.1f%cC * Tomorrow: %s, Min: %.1f%cC, Max: %.1f%cC *",
           ALTITUDE,                                    // Altitude
           localTime,                                   // Current time
           PM25_actual,                                 // PM2.5 sensor
           TEMP_actual,                                 // Local temperature sensor
           (char)123,                                   // Degree symbol for local temp
           getWeatherDescription(currentWeatherCode).c_str(),  // Current weather
           currentTemp,                                 // Current temperature from OpenMeteo
           (char)123,                                   // Degree symbol for current temp
           getWeatherDescription(tomorrowWeatherCode).c_str(), // Tomorrow's weather
           tomorrowMinTemp,                             // Tomorrow's min temp
           (char)123,                                   // Degree symbol for min temp
           tomorrowMaxTemp,                             // Tomorrow's max temp
           (char)123);                                  // Degree symbol for max temp

  scrollMessage(combinedMsg, CRGB(255, 20, 147));
  
  delay(100);
}
