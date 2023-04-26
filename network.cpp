#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "HttpClient.h"
#include <ArduinoJson.h>
#include "bus_description.h"

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30*1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

char kHostname[] = "api.sl.se";
char kPath[] = "/api2/realtimedeparturesV4.json?siteid=%d&timewindow=20&key=%s";

int connectWifi() {

  Serial.println("Setup");
  
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    return -1;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to Wifi network:
  
  Serial.print("Attempting to connect to WPA SSID: ");
  Serial.println(ssid);
  // Connect to WPA/WPA2 network:
  status = WiFi.begin(ssid, pass);

  for (int waits = 0; status != WL_CONNECTED; waits++) {
    if (waits > 300) {
      return -2;
    }
    delay(100);
  }

  Serial.print("Connected to the network");
  return 0;
}

BusResults queryWebService() {
  Serial.println("\nStarting connection to server...");
  WiFiSSLClient client;
  HttpClient http(client);

  int stop_id = 4028;
  char path_buffer[96];
  sprintf(path_buffer, kPath, stop_id, SECRET_SL_API_KEY);

  int err = http.get(kHostname, 443, path_buffer);
  if (err != 0) {
    Serial.print("connect failed: ");
    Serial.println(err);
    return BusResults{result:err};
  }
  Serial.println("startedRequest ok");

  err = http.responseStatusCode();
  if (err != 200) {
    Serial.print("Get returned status code: ");
    Serial.println(err);
    return BusResults{result:err};
  }

  err = http.skipResponseHeaders();
  if (err != 0) {
    Serial.print("Failed skipping headers: ");
    Serial.println(err);
    return BusResults{result:err};
  }

  int bodyLen = http.contentLength();
  Serial.print("Content length is: ");
  Serial.println(bodyLen);
  Serial.println();

  unsigned long timeoutStart = millis();

  char buffer[5000];
  char* curr_buffer_pos = buffer;
  if (bodyLen > 5000) {
    return BusResults{result:-4};
  } 

  char c;
  // Whilst we haven't timed out & haven't reached the end of the body
  while ( (http.connected() || http.available()) &&
          ((millis() - timeoutStart) < kNetworkTimeout) && bodyLen) {
      if (http.available()) {
          c = http.read();
          // Print out this character
          Serial.print(c);
          *curr_buffer_pos = c;
          curr_buffer_pos++;
          
          bodyLen--;
          // We read something, reset the timeout counter
          timeoutStart = millis();
      } else {
          // We haven't got any data, so let's pause to allow some to
          // arrive
          delay(kNetworkDelay);
      }
  }
  http.stop();

  if (bodyLen) {
    // Didn't read the full body.
    return BusResults{result: -7};
  }

  Serial.println();
  Serial.println("Buffer:");
  *curr_buffer_pos = '\0';
  Serial.println(buffer);

  StaticJsonDocument<5000> json_doc;
  
  DeserializationError json_error = deserializeJson(json_doc, buffer);

  // Test if parsing succeeds.
  if (json_error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(json_error.f_str());
    return BusResults{result:-5};
  }

  Serial.print("StatusCode: ");
  Serial.println(int(json_doc["StatusCode"]));

  Serial.print("Results: ");
  int res_count = json_doc["ResponseData"]["Buses"].size();

  BusDescription* descs = (BusDescription*)(malloc(sizeof(BusDescription) * res_count));
  if (descs <= 0) {
    return BusResults{result: -6};
  }

  int matching_busses = 0;
  for (int i = 0; i < res_count; i++) {
    JsonObject bus = json_doc["ResponseData"]["Buses"][i];

    if(bus["JourneyDirection"] != 2)
      continue; // Probably means it's not going into town.

    const char* line_number = bus["LineNumber"];
    memcpy(&descs[matching_busses].number, line_number, 3);

    const char* expected_time = bus["ExpectedDateTime"];
    memcpy(&descs[matching_busses].time, expected_time + 11, 5);
    matching_busses++;
  }

  return BusResults{
    result: 0,
    descs: descs,
    len: matching_busses,
  };
}

int endWifi() {
  WiFi.end();
  return 0;
}