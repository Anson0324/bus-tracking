#include <WiFi.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <ezTime.h>

// Define Serial Port for GPS
HardwareSerial GPS_Serial(1);

// GPIO Definitions and Baud Rates
#define GPS_TX_PIN 21
#define GPS_RX_PIN 20
#define BAUD_RATE 9600

// Bus ID
String busID = "bus2";

//Timezone
Timezone myTZ;

// WiFi Credentials
const char* ssid = "HelloWorld1215";   // Your WiFi SSID (Hotspot Name)
const char* password = "fastmike";     // Your WiFi Password

// Firebase Database Credentials
const char* FIREBASE_HOST = "https://anson-gps-default-rtdb.firebaseio.com/"; 
const char* FIREBASE_AUTH = "QSGTyZrSUe9y8xXwqJrWXlNPPrl2Ayy8ss21ysD4";

// Function to convert NMEA format to Decimal Degrees
float convertNmeaToDecimal(String coordinate, String direction) {
  if (coordinate.length() < 6) {
    Serial.println("Invalid coordinate format.");
    return 0.0;
  }

  int degLength = (coordinate.indexOf('.') > 4) ? 3 : 2;  // Latitude has 2, Longitude has 3
  String degreesStr = coordinate.substring(0, degLength);
  String minutesStr = coordinate.substring(degLength);

  float degrees = degreesStr.toFloat();
  float minutes = minutesStr.toFloat();
  float decimal = degrees + (minutes / 60.0);

  // If South (S) or West (W), make negative
  if (direction == "S" || direction == "W") {
    decimal = -decimal;
  }


  return decimal;
}

// Function to connect to WiFi with retries
void connectToWiFi() {
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid, password);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    Serial.print("Connecting to WiFi");
    int attempts = 0;

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        attempts++;

        // If WiFi fails for 30 seconds, restart ESP32
        if (attempts > 60) {
            Serial.println("\nFailed to connect to WiFi! Restarting ESP32...");
            ESP.restart();
        }
    }

    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}


void setup() {
  Serial.begin(115200);
  Serial.println("GPS Tracker Started...");

  // Initialize GPS Serial
  GPS_Serial.begin(BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("Waiting for GPS Data...");

  // Connect to WiFi
  connectToWiFi();

  //time
  waitForSync();     // Wait for NTP sync
  myTZ.setLocation("America/Los_Angeles"); // Auto-handles PST/PDT
}

void loop() {
  if (GPS_Serial.available()) {
    String nmeaSentence = GPS_Serial.readStringUntil('\n'); // Read one line
    if (nmeaSentence.startsWith("$GNGGA")) { // Process only GNGGA sentences
      int commaIndex = 0, lastIndex = 0;
      String parts[15];

      for (int i = 0; i < 15; i++) {
        commaIndex = nmeaSentence.indexOf(',', lastIndex);
        if (commaIndex == -1) break;
        parts[i] = nmeaSentence.substring(lastIndex, commaIndex);
        lastIndex = commaIndex + 1;
      }

      int fixQuality = parts[6].toInt();
      if (fixQuality == 0) {
        Serial.println("No valid GPS fix.");
        return;
      }

      // Extract Raw NMEA GPS Coordinates
      String latStr = parts[2];
      String latDir = parts[3];
      String lonStr = parts[4];
      String lonDir = parts[5];


      // Convert to Decimal Degrees
      float lat = convertNmeaToDecimal(latStr, latDir);
      float lon = convertNmeaToDecimal(lonStr, lonDir);

      // Ensure valid latitude & longitude values before sending
      if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
        Serial.println("Invalid GPS coordinates, skipping Firebase upload.");
        return;
      }

      // Check if WiFi is still connected
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi lost, attempting to reconnect...");
        WiFi.reconnect();  // Attempt reconnect
        delay(2000); // Wait before retrying
      }

      if (WiFi.status() == WL_CONNECTED) {

        // Create Firebase URL that writes to the fixed path for this bus
        String firebaseUrl = String(FIREBASE_HOST) + "/locations/" + busID + ".json?auth=" + FIREBASE_AUTH;

        // JSON Payload with latitude and longitude
        String payload = 
        "{\"lat\":" + String(lat, 6) + 
        ", \"lng\":" + String(lon, 6) + 
        ", \"timestamp\":\"" + myTZ.dateTime("M-d H:i") + "\"}";

        Serial.print("Bus ID: ");
        Serial.println(busID);
        Serial.print("Sending to Firebase: ");
        Serial.println(firebaseUrl);
        Serial.print("Payload: ");
        Serial.println(payload);
        Serial.println();

        // Add a 1-second delay before sending the PUT request
        delay(1000);

        // Send data to Firebase using PUT to update the fixed path
        HTTPClient http;
        http.begin(firebaseUrl);
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.PUT(payload);

        if (httpResponseCode > 0) {
          Serial.print("Firebase Response code: ");
          Serial.println(httpResponseCode);
          String response = http.getString();
          Serial.println("Response: " + response);
        } else {
          Serial.print("Error sending to Firebase: ");
          Serial.println(httpResponseCode);
        }

        http.end();
      } else {
        Serial.println("WiFi not connected, skipping Firebase upload.");
      }
    }
  }
}