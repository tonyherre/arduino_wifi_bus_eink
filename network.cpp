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

bool Earlier(BusDescription* bus1, BusDescription* bus2) {
  if (bus1->time[0] < bus2->time[0])
    return true;
  if (bus1->time[0] == bus2->time[0] && bus1->time[1] < bus2->time[1])
    return true;
  // time[2] is a colon.
  if (bus1->time[0] == bus2->time[0] && bus1->time[1] == bus2->time[1] && bus1->time[3] < bus2->time[3])
    return true;
  if (bus1->time[0] == bus2->time[0] && bus1->time[1] == bus2->time[1] && bus1->time[3] == bus2->time[3] && bus1->time[4] < bus2->time[4])
    return true;
  return false;
}

BusResults querySingleStop(int stop_id) {
  Serial.println("\nStarting connection to server...");
  WiFiSSLClient client;
  HttpClient http(client);

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

  unsigned long timeoutStart = millis();

  char buffer[10000];
  char* curr_buffer_pos = buffer;
  if (bodyLen > 10000) {
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

  *curr_buffer_pos = '\0';

  StaticJsonDocument<5000> json_doc;
  
  DeserializationError json_error = deserializeJson(json_doc, buffer);

  // Test if parsing succeeds.
  if (json_error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(json_error.f_str());
    return BusResults{result:-5};
  }

  Serial.print("StatusCode: ");
  int status_code = json_doc["StatusCode"];
  Serial.println(status_code);

  if (status_code != 0) {
    return BusResults{result:status_code};
  }

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

    descs[matching_busses].journey_number = bus["JourneyNumber"];
    descs[matching_busses].stop_id = stop_id;

    matching_busses++;
  }

  return BusResults{
    result: 0,
    descs: descs,
    len: matching_busses,
  };
}


BusResults queryWebService() {
  BusResults results1 = querySingleStop(4010);
  if (results1.result != 0) {
    Serial.println("Stop1 failed");
    return results1;
  }
  Serial.print("Stop1 gave result count: ");
  Serial.println(results1.len);
  BusResults results2 = querySingleStop(4028);
  if (results2.result != 0) {
    Serial.println("Stop2 failed");
    free(results1.descs);
    return results2;
  }
  Serial.print("Stop2 gave result count: ");
  Serial.println(results2.len);
  BusResults results3 = querySingleStop(4027);
  if (results3.result != 0) {
    Serial.println("Stop3 failed");
    free(results1.descs);
    free(results2.descs);
    return results3;
  }
  Serial.print("Stop3 gave result count: ");
  Serial.println(results3.len);

  // First dedupe based on journey_number, to handle when a single bus stops at multiple of our stops.
  // Take the earliest stop, which is usually right for us.
  int max_result = results1.len + results2.len + results3.len;
  BusDescription* combined_descs = (BusDescription*)(malloc(sizeof(BusDescription) * max_result));
  int unique_result_count = results1.len;
  for(int i = 0; i < results1.len; i++) {
    combined_descs[i] = results1.descs[i];
  }
  free(results1.descs);

  for(int i = 0; i < results2.len; i++) {
    bool already_included = false;
    for (int j = 0; j < unique_result_count; j++) {
      if (results2.descs[i].journey_number == combined_descs[j].journey_number) {
        if (Earlier(&results2.descs[i], &combined_descs[j])) {
          memcpy(combined_descs[j].time, results2.descs[i].time, 5);
          combined_descs[j].stop_id = results2.descs[i].stop_id;
        }
        already_included = true;
        break;
      }
    }
    if (!already_included) {
      combined_descs[unique_result_count++] = results2.descs[i];
    }
  }
  free(results2.descs);

  for(int i = 0; i < results3.len; i++) {
    bool already_included = false;
    for (int j = 0; j < unique_result_count; j++) {
      if (results3.descs[i].journey_number == combined_descs[j].journey_number) {
        if (Earlier(&results3.descs[i], &combined_descs[j])) {
          memcpy(combined_descs[j].time, results3.descs[i].time, 5);
          combined_descs[j].stop_id = results3.descs[i].stop_id;
        }
        already_included = true;
        break;
      }
    }
    if (!already_included) {
      combined_descs[unique_result_count++] = results3.descs[i];
    }
  }
  free(results3.descs);

  Serial.print("Combined to give a result count of : ");
  Serial.println(unique_result_count);

  // Bubble sort 'cos I'm lazy (and the list is always small anyway).
  while(true) {
    bool done = true;
    for (int j = 0; j < unique_result_count-1; j++) {
      if (Earlier(&combined_descs[j+1], &combined_descs[j])) {
        BusDescription temp = combined_descs[j];
        combined_descs[j] = combined_descs[j+1];
        combined_descs[j+1] = temp;
        done = false;
      }
    }
    if (done) {
      break;
    }
  }

  return BusResults{result: 0, descs: combined_descs, len:unique_result_count};
}

int endWifi() {
  WiFi.end();
  return 0;
}