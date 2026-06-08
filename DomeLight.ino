/********************************************************************
 * V9.1.0: Final Architecture - Compile-Time Mode Selection (HTTPS+BLE).
 * - This version resolves "multiple definition" errors by using
 * preprocessor directives to select the server mode at compile time.
 * - Restored missing function prototypes.
 * - #define ENABLE_HTTPS 1 -> Compiles in HTTPS + BLE mode.
 * - #define ENABLE_HTTPS 0 -> Compiles in HTTP-Only mode.
 *******************************************************************/

// =================================================================
//                    !!! MODE SELECTION SWITCH !!!
// =================================================================
// Set to 1 to compile for HTTPS + Bluetooth mode.
// Set to 0 to compile for HTTP-Only mode (Bluetooth disabled).
#define ENABLE_HTTPS 1
// =================================================================

#include <Arduino.h>

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,0,0)
#include <ETHClass2.h>
#define ETH  ETH2
#else
#include <ETH.h>
#endif

#include <SPI.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <AsyncMQTT_ESP32.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "time.h"
#include <Update.h>
#include "esp_partition.h"
#include "FS.h"
#include "FFat.h"
#include "web.h"

// Conditionally include server, certs, and BLE libraries
#if (ENABLE_HTTPS == 1)
#include <ESPWebServerSecure.hpp>
#include "device_cert.h"
#include "device_key.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#else
#include <WebServer.h>
#endif

// --- Firmware Version Definition ---
#if (ENABLE_HTTPS == 1)
const char* FIRMWARE_VERSION = "FL_V_9.1.0_HTTPS_BLE";
#else
const char* FIRMWARE_VERSION = "FL_V_9.1.0_HTTP_ONLY";
#endif

// --- Login Function Variables ---
const char* www_username = "admin";
const char* www_password = "password";
bool session_authenticated = false;
bool needRestart = false;

// ===== Global Variables and Objects =====
static bool eth_connected = false;

// Conditionally define the server object
#if (ENABLE_HTTPS == 1)
ESPWebServerSecure server(443);
BLEScan* pBLEScan;
#else
WebServer server(80);
#endif

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
WiFiUDP Udp;

Preferences preferences;
StaticJsonDocument<1024> doc;
StaticJsonDocument<1536> api_doc;
StaticJsonDocument<512> mqttJsonDoc;
bool upload_error = false;
bool network_services_initialized = false;

// ===== Configuration Variables =====
bool eth_dhcp_enabled = true;
char eth_static_ip[16];
char eth_subnet[16];
char eth_gateway[16];

#define ETH_MISO_PIN                    11
#define ETH_MOSI_PIN                    12
#define ETH_SCLK_PIN                    10
#define ETH_CS_PIN                      9
#define ETH_INT_PIN                     13
#define ETH_RST_PIN                     14
#define ETH_ADDR                        1

char mqtt_server[64];
int  mqtt_port;
char mqtt_topic[128];
char nurse_station_id[32];
char room_ids[60][8];
int  room_count = 0;
char ntp_server[64];

const int udp_port = 2269;
#define LED_OFF     0
#define LED_RED     1
#define LED_BLUE    2
#define LED_YELLOW  3
#define LED_GREEN   4

#define STATUS_IDLE     0
#define STATUS_SA       1
#define STATUS_CF       2
#define STATUS_BA       3
#define STATUS_CB_E_BE  4
#define STATUS_E_BE     5
#define STATUS_CB       6
#define STATUS_TEST     7

int GPIO_BUZZ = 48;
int GPIO_LAMP_R = 9;
int GPIO_LAMP_G = 20;
int GPIO_LAMP_B = 19;
int GPIO_BUTTON = 47;

int has_cb = 0, has_e_be = 0, has_ba = 0, has_cf = 0, has_sa = 0;
int now_light_status = STATUS_IDLE;
int last_light_status = STATUS_IDLE;
int cnt_500ms, cnt_1s;
int flag_500ms, flag_1s;
int buzzer_ticks, buzzer_on_off, light_color;
hw_timer_t *timer1 = NULL;

typedef struct {
  char room[20];
  char bed[20];
  int callingType;
} record_alarm;

#define RECORD_ALARM_POOL_SIZE 60
record_alarm alarm_list[RECORD_ALARM_POOL_SIZE];

// ===== Restored Function Prototypes =====
void loadSettings();
void initializeNetworkServices();
void onMqttConnect(bool sessionPresent);
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
void handleRoot();
void handleNotFound();
void handleLoginPage();
void handleDoLogin();
void handleLogout();
void handleSetting();
void handleSave();
void handleFWUploadPage();
void handleHWTest();
void handleApiStatus();
void WiFiEvent(arduino_event_id_t event);
void GPIO_LED_SET(int color);
void IRAM_ATTR buzzerModule();
void exitHardwareTestMode();
void updateAlarmStatus();
void processAlarmPayload(const char* message);
void syncNTP();
void connectToMqtt();

// ===== BLE Callback Class (Only compiled if HTTPS is enabled) =====
#if (ENABLE_HTTPS == 1)
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveManufacturerData() == true) {
            std::string strManufacturerData = advertisedDevice.getManufacturerData();
            if (strManufacturerData.length() >= 2 && (uint8_t)strManufacturerData[0] == 0x13 && (uint8_t)strManufacturerData[1] == 0x00) {
                if (strManufacturerData.length() >= 18) {
                    Serial.println("--- Found Target Beacon ---");
                    uint8_t watch_id = strManufacturerData[4];
                    uint8_t action = strManufacturerData[8];
                    uint8_t battery_level = strManufacturerData[17];
                    int rssi = advertisedDevice.getRSSI();
                    std::string beacon_mac_str = advertisedDevice.getAddress().toString();
                    
                    Serial.printf("  Address: %s, RSSI: %d\n", beacon_mac_str.c_str(), rssi);
                    Serial.printf("  Watch ID: %d, Action: %d, Battery: %d\n", watch_id, action, battery_level);
                    
                    mqttJsonDoc.clear();
                    String flashLightId = ETH.macAddress();
                    mqttJsonDoc["sipNumber"] = flashLightId;
                    mqttJsonDoc["nurseStation"] = String(nurse_station_id);
                    mqttJsonDoc["room"] = flashLightId;
                    mqttJsonDoc["bed"] = flashLightId;
                    char idStr[3];
                    sprintf(idStr, "%02X", watch_id);
                    mqttJsonDoc["id"] = idStr;
                    mqttJsonDoc["rssi"] = String(rssi);
                    mqttJsonDoc["mac"] = beacon_mac_str;
                    char batteryStr[3];
                    sprintf(batteryStr, "%02X", battery_level);
                    mqttJsonDoc["battery"] = batteryStr;
                    char actionStr[3];
                    sprintf(actionStr, "%02X", action);
                    mqttJsonDoc["action"] = actionStr;
                    mqttJsonDoc["deviceType"] = "00";
                    
                    char jsonBuffer[512];
                    serializeJson(mqttJsonDoc, jsonBuffer);
                    if (mqttClient.connected()) {
                        const char* topic = "Beacon/rssi";
                        mqttClient.publish(topic, 1, false, jsonBuffer);
                        Serial.printf("  Published to MQTT topic '%s': %s\n", topic, jsonBuffer);
                    }
                }
            }
        }
    }
};
#endif

// ===== Core Network Event Handler =====
void WiFiEvent(arduino_event_id_t event) {
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("ETH Started");
        ETH.setHostname("esp32-flash-light");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.print("ETH MAC: ");
        Serial.print(ETH.macAddress());
        Serial.print(", IPv4: ");
        Serial.print(ETH.localIP());
        if (ETH.fullDuplex()) {
            Serial.print(", FULL_DUPLEX");
        }
        Serial.print(", ");
        Serial.print(ETH.linkSpeed());
        Serial.println("Mbps");
        eth_connected = true;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("ETH Disconnected");
        eth_connected = false;
        network_services_initialized = false;
        if (mqttClient.connected()) {
            mqttClient.disconnect();
        }
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("ETH Stopped");
        eth_connected = false;
        network_services_initialized = false;
        break;
    default:
        break;
    }
}

// ===== Core Logic =====
void GPIO_LED_SET(int color) {
  switch(color){
    case LED_OFF:    digitalWrite(GPIO_LAMP_R, LOW);  digitalWrite(GPIO_LAMP_G, LOW);  digitalWrite(GPIO_LAMP_B, LOW);  break;
    case LED_RED:    digitalWrite(GPIO_LAMP_R, HIGH); digitalWrite(GPIO_LAMP_G, LOW);  digitalWrite(GPIO_LAMP_B, LOW);  break;
    case LED_BLUE:   digitalWrite(GPIO_LAMP_R, LOW);  digitalWrite(GPIO_LAMP_G, LOW);  digitalWrite(GPIO_LAMP_B, HIGH); break;
    case LED_YELLOW: digitalWrite(GPIO_LAMP_R, HIGH); digitalWrite(GPIO_LAMP_G, HIGH); digitalWrite(GPIO_LAMP_B, LOW);  break;
    case LED_GREEN:  digitalWrite(GPIO_LAMP_R, LOW);  digitalWrite(GPIO_LAMP_G, HIGH); digitalWrite(GPIO_LAMP_B, LOW);  break;
  }
}

void IRAM_ATTR buzzerModule() {
  if(now_light_status > STATUS_IDLE && now_light_status < STATUS_TEST) {
    cnt_500ms++; cnt_1s++;
    if(cnt_500ms > 4) { cnt_500ms = 0; flag_500ms = 1; }
    if(cnt_1s > 9) { cnt_1s = 0; flag_1s = 1; }
    if(buzzer_ticks > 0) { buzzer_ticks--; }
  }
  
  switch(now_light_status) {
    case STATUS_SA: light_color = LED_YELLOW; GPIO_LED_SET(light_color); break;
    case STATUS_CF:
        light_color = LED_GREEN; GPIO_LED_SET(light_color);
        if(buzzer_on_off == 0 && buzzer_ticks == 0) { buzzer_on_off = 1; buzzer_ticks = 9; digitalWrite(GPIO_BUZZ, HIGH); }
        else if(buzzer_on_off == 1 && buzzer_ticks == 0) { buzzer_on_off = 0; buzzer_ticks = 29; digitalWrite(GPIO_BUZZ, LOW); }
        break;
    case STATUS_BA: light_color = LED_YELLOW; GPIO_LED_SET(light_color); break;
    case STATUS_CB_E_BE:
        if(light_color == LED_OFF) { light_color = LED_RED; GPIO_LED_SET(light_color); }
        else if((light_color == LED_RED) && (flag_1s == 1)) { flag_1s = 0; light_color = LED_BLUE; GPIO_LED_SET(light_color); }
        else if((light_color == LED_BLUE) && (flag_1s == 1)) { flag_1s = 0; light_color = LED_RED; GPIO_LED_SET(light_color); }
        
        if(buzzer_on_off == 0 && buzzer_ticks == 0) { buzzer_on_off = 1; buzzer_ticks = 0; digitalWrite(GPIO_BUZZ, HIGH); }
        else if(buzzer_on_off == 1 && buzzer_ticks == 0) { buzzer_on_off = 0; buzzer_ticks = 0; digitalWrite(GPIO_BUZZ, LOW); }
        break;
    case STATUS_E_BE:
        light_color = LED_RED; GPIO_LED_SET(light_color);
        if(buzzer_on_off == 0 && buzzer_ticks == 0) { buzzer_on_off = 1; buzzer_ticks = 4; digitalWrite(GPIO_BUZZ, HIGH); }
        else if(buzzer_on_off == 1 && buzzer_ticks == 0) { buzzer_on_off = 0; buzzer_ticks = 4; digitalWrite(GPIO_BUZZ, LOW); }
        break;
    case STATUS_CB:
        light_color = LED_BLUE; GPIO_LED_SET(light_color);
        if(buzzer_on_off == 0 && buzzer_ticks == 0) { buzzer_on_off = 1; buzzer_ticks = 4; digitalWrite(GPIO_BUZZ, HIGH); }
        else if(buzzer_on_off == 1 && buzzer_ticks == 0) { buzzer_on_off = 0; buzzer_ticks = 0; digitalWrite(GPIO_BUZZ, LOW); }
        break;
    case STATUS_IDLE:
        cnt_500ms=0; cnt_1s=0; flag_500ms=0; flag_1s=0; buzzer_on_off=0; buzzer_ticks=0;
        light_color = LED_OFF; GPIO_LED_SET(light_color);
        digitalWrite(GPIO_BUZZ, LOW);
        break;
  }
}

void exitHardwareTestMode() {
    if (now_light_status == STATUS_TEST) {
        Serial.println("Exiting hardware test mode.");
        digitalWrite(GPIO_BUZZ, LOW);
        digitalWrite(GPIO_LAMP_R, LOW);
        digitalWrite(GPIO_LAMP_G, LOW);
        digitalWrite(GPIO_LAMP_B, LOW);
        digitalWrite(GPIO_BUTTON, LOW);
        now_light_status = STATUS_IDLE;
        updateAlarmStatus();
    }
}

void updateAlarmStatus() {
    if (now_light_status == STATUS_TEST) return;
    has_cb = 0; has_e_be = 0; has_ba = 0; has_cf = 0; has_sa = 0;
    
    for (int i = 0; i < RECORD_ALARM_POOL_SIZE; i++) {
        switch (alarm_list[i].callingType) {
            case 0:   has_sa = 1; break;
            case 100: has_cf = 1; break;
            case 300: has_ba = 1; break;
            case 400: case 500: has_e_be = 1; break;
            case 800: has_cb = 1; break;
        }
    }
    
    if (has_cb && has_e_be) now_light_status = STATUS_CB_E_BE;
    else if (has_cb) now_light_status = STATUS_CB;
    else if (has_e_be) now_light_status = STATUS_E_BE;
    else if (has_ba) now_light_status = STATUS_BA;
    else if (has_cf) now_light_status = STATUS_CF;
    else if (has_sa) now_light_status = STATUS_SA;
    else now_light_status = STATUS_IDLE;
    
    if (last_light_status != now_light_status) {
        Serial.printf("Light status changed from %d to %d\n", last_light_status, now_light_status);
        last_light_status = now_light_status;
        cnt_500ms=0; cnt_1s=0; flag_500ms=0; flag_1s=0; buzzer_on_off=0; buzzer_ticks=0; light_color = LED_OFF;
    }
}

void processAlarmPayload(const char* message) {
  DeserializationError error = deserializeJson(doc, message);
  if (error) { Serial.print(F("deserializeJson() failed: ")); Serial.println(error.c_str()); return; }
  
  const char* msg_nsS = doc["nurseStation"];
  const char* msg_room = doc["room"];
  
  if (!msg_nsS || !msg_room) {
    Serial.println("Payload missing 'nurseStation' or 'room'. Ignoring.");
    return;
  }
  
  if (strcmp(msg_nsS, nurse_station_id) != 0) {
    Serial.println("Message not for this Nurse Station. Ignoring.");
    return;
  }
  
  bool room_match = false;
  if (room_count > 0) {
    for (int i = 0; i < room_count; i++) {
      if (strcmp(msg_room, room_ids[i]) == 0) {
        room_match = true;
        break;
      }
    }
  } else {
      room_match = true;
      Serial.println("No room IDs configured, accepting message for this nurse station.");
  }
  
  if (!room_match) {
    Serial.println("Message not for a configured Room. Ignoring.");
    return;
  }
  
  Serial.println("Message is for this device. Processing...");
  const char* msg_bed = doc["bed"];
  int callingType = doc["callingType"];
  int action = doc["action"];
  
  if (action == 1) {
    bool is_exist = false;
    for(int i=0; i < RECORD_ALARM_POOL_SIZE; i++) { 
        if (alarm_list[i].callingType != -1 && strcmp(alarm_list[i].room, msg_room) == 0 && strcmp(alarm_list[i].bed, msg_bed) == 0 && alarm_list[i].callingType == callingType) { 
            is_exist = true; break; 
        }
    }
    
    if (!is_exist) { 
        for(int i=0; i < RECORD_ALARM_POOL_SIZE; i++) { 
            if (alarm_list[i].callingType == -1) { 
                strcpy(alarm_list[i].room, msg_room);
                strcpy(alarm_list[i].bed, msg_bed); 
                alarm_list[i].callingType = callingType; 
                Serial.printf("Added alarm: Room %s, Bed %s, Type %d\n", msg_room, msg_bed, callingType); 
                updateAlarmStatus(); 
                return;
            } 
        } 
        Serial.println("Alarm list is full!"); 
    }
  } else {
    for(int i=0; i < RECORD_ALARM_POOL_SIZE; i++) { 
        if (alarm_list[i].callingType != -1 && strcmp(alarm_list[i].room, msg_room) == 0 && strcmp(alarm_list[i].bed, msg_bed) == 0 && alarm_list[i].callingType == callingType) { 
            alarm_list[i].callingType = -1;
            strcpy(alarm_list[i].room, ""); 
            strcpy(alarm_list[i].bed, ""); 
            Serial.printf("Removed alarm: Room %s, Bed %s, Type %d\n", msg_room, msg_bed, callingType); 
            updateAlarmStatus(); 
            return;
        } 
    }
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  payload[len] = '\0';
  Serial.print("MQTT Message arrived ["); Serial.print(topic); Serial.print("] "); Serial.println(payload);
  processAlarmPayload(payload);
}

// ===== Setup and Connection =====
void loadSettings() {
  Serial.println("Loading settings from NVS...");
  preferences.begin("flash-light", false);
  eth_dhcp_enabled = preferences.getBool("eth_dhcp_en", true);
  preferences.getString("eth_static_ip", eth_static_ip, sizeof(eth_static_ip));
  preferences.getString("eth_subnet", eth_subnet, sizeof(eth_subnet));
  preferences.getString("eth_gateway", eth_gateway, sizeof(eth_gateway));
  
  String mqtt_server_str = preferences.getString("mqtt_server", "192.168.1.100");
  strncpy(mqtt_server, mqtt_server_str.c_str(), sizeof(mqtt_server) - 1);
  mqtt_server[sizeof(mqtt_server)-1] = '\0';
  mqtt_port = preferences.getInt("mqtt_port", 1883);
  
  String topic_str = preferences.getString("mqtt_topic", "NurseCall/Warning/NurseStation/#");
  strncpy(mqtt_topic, topic_str.c_str(), sizeof(mqtt_topic) - 1); 
  mqtt_topic[sizeof(mqtt_topic)-1] = '\0';
  
  String ns_id_str = preferences.getString("ns_id", "1A");
  strncpy(nurse_station_id, ns_id_str.c_str(), sizeof(nurse_station_id) - 1);
  nurse_station_id[sizeof(nurse_station_id)-1] = '\0';
  
  room_count = preferences.getInt("room_count", 0);
  for (int i = 0; i < 60; i++) { strcpy(room_ids[i], ""); }
  
  for (int i = 0; i < room_count; i++) {
    String key = "room_" + String(i);
    preferences.getString(key.c_str(), room_ids[i], sizeof(room_ids[i]));
  }
  
  String ntp_server_str = preferences.getString("ntp_server", "pool.ntp.org");
  strncpy(ntp_server, ntp_server_str.c_str(), sizeof(ntp_server) - 1); 
  ntp_server[sizeof(ntp_server)-1] = '\0';
  
  preferences.end();
  Serial.println("Settings loaded.");
}

void syncNTP() {
  if (strlen(ntp_server) > 0) {
    Serial.print("Syncing time with ");
    Serial.println(ntp_server);
    configTime(8 * 3600, 0, ntp_server);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      Serial.println("Time synced successfully.");
    } else {
      Serial.println("Failed to obtain time.");
    }
  }
}

void connectToMqtt() {
  if (eth_connected) {
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
  } else {
    Serial.println("Skipping MQTT connection, no network.");
  }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  if (strlen(mqtt_topic) > 0) {
      mqttClient.subscribe(mqtt_topic, 2);
      Serial.print("Subscribing to: "); Serial.println(mqtt_topic);
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");
  if (eth_connected) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void initializeNetworkServices() {
    if (network_services_initialized) return;
    if (eth_connected) {
        Serial.println("Initializing network services...");
        server.on("/", HTTP_GET, handleLoginPage);
        server.on("/login", HTTP_POST, handleDoLogin);
        server.on("/logout", HTTP_GET, handleLogout);
        server.on("/setting", HTTP_GET, handleSetting);
        server.on("/save", HTTP_POST, handleSave);
        server.on("/fwupload", HTTP_GET, handleFWUploadPage);
        server.on("/hw_test", HTTP_GET, handleHWTest);
        server.on("/api/status", HTTP_GET, handleApiStatus);
        server.on("/update", HTTP_POST, []() {
            if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
            server.sendHeader("Connection", "close");
            if (upload_error) { server.send(400, "text/plain", "INVALID FILE TYPE: Please upload a .bin file."); }
            else { server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE SUCCESS! Rebooting..."); }
            delay(3000);
            if (!Update.hasError() && !upload_error) { needRestart = true; }
            upload_error = false;
            }, []() {
            if (!session_authenticated) return;
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                upload_error = false;
                Serial.printf("Update: %s\n", upload.filename.c_str());
                if (!upload.filename.endsWith(".bin")) { 
                    Serial.println("Invalid file extension. Aborting update.");
                    upload_error = true; return; 
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { 
                    Update.printError(Serial);
                    upload_error = true; 
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (upload_error) return;
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { 
                    Update.printError(Serial); upload_error = true;
                }
                yield();
            } else if (upload.status == UPLOAD_FILE_END) {
                if (upload_error) return;
                if (Update.end(true)) { Serial.printf("Update Success: %u\n", upload.totalSize); }
                else { Update.printError(Serial); }
            }
        });
        
        server.onNotFound(handleNotFound);

        #if (ENABLE_HTTPS == 1)
        Serial.println("Loading SSL certificate and key for HTTPS server...");
        server.setServerKeyAndCert((const uint8_t*)fallback_key_pem, strlen(fallback_key_pem), (const uint8_t*)fallback_cert_pem, strlen(fallback_cert_pem));
        #endif

        server.begin();
        
        #if (ENABLE_HTTPS == 1)
        Serial.println("HTTPS server started on port 443");
        #else
        Serial.println("HTTP server started on port 80");
        #endif

        syncNTP();
        Udp.begin(udp_port);
        Serial.printf("UDP listener started on port %d\n", udp_port);
        connectToMqtt();
        network_services_initialized = true;
    } else {
        Serial.println("Skipping network services initialization: No active connection.");
    }
}

// ===== Web Server Handlers =====
void handleRoot() {
    server.send(200, "text/plain", "hello from esp32!");
}

void handleNotFound() {
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

void handleLoginPage() {
    String login_page = FPSTR(HTML_LOGIN_PAGE);
    if (server.hasArg("error")) {
        login_page.replace("%ERROR%", "Invalid username or password.");
    } else {
        login_page.replace("%ERROR%", "");
    }
    server.send(200, "text/html", login_page);
}

void handleDoLogin() {
    if (server.hasArg("username") && server.hasArg("password") &&
        server.arg("username") == String(www_username) &&
        server.arg("password") == String(www_password)) {
        session_authenticated = true;
        server.sendHeader("Location", "/setting", true);
        server.send(302, "text/plain", "");
        Serial.println("User login successful.");
    } else {
        session_authenticated = false;
        server.sendHeader("Location", "/?error=1", true);
        server.send(302, "text/plain", "");
        Serial.println("User login failed.");
    }
}

void handleLogout() {
    exitHardwareTestMode();
    session_authenticated = false;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
    Serial.println("User logged out.");
}

void handleSetting() {
    exitHardwareTestMode();
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(FPSTR(HTML_HEADER));
    String buffer;
    buffer.reserve(3072);
    buffer = "<h3>Current Firmware Version: " + String(FIRMWARE_VERSION) + "</h3>";
    server.sendContent(buffer);
    
    buffer = "<h2>1. Mode Information</h2>";
    #if (ENABLE_HTTPS == 1)
    buffer += "<p><b>Current Mode:</b> HTTPS (Bluetooth Enabled)</p>";
    buffer += "<p>To enable HTTP only, change ENABLE_HTTPS to 0 in the code and recompile.</p>";
    #else
    buffer += "<p><b>Current Mode:</b> HTTP Only (Bluetooth Disabled)</p>";
    buffer += "<p>To enable HTTPS and Bluetooth, change ENABLE_HTTPS to 1 in the code and recompile.</p>";
    #endif
    server.sendContent(buffer);

    buffer = "<h2>2. Ethernet Settings</h2><form action='/save' method='post'>";
    buffer += "<p>Current IP: " + (eth_connected ? ETH.localIP().toString() : "Disconnected") + "</p>";
    buffer += "<label>DHCP</label><input type='radio' name='eth_dhcp' value='1' " + String(eth_dhcp_enabled ? "checked" : "") + "><br>";
    buffer += "<label>Static IP</label><input type='radio' name='eth_dhcp' value='0' " + String(eth_dhcp_enabled ? "" : "checked") + "><hr>";
    buffer += "<label>IP Address:</label><input type='text' name='eth_static_ip' value='" + String(eth_static_ip) + "'><br>";
    buffer += "<label>Subnet Mask:</label><input type='text' name='eth_subnet' value='" + String(eth_subnet) + "'><br>";
    buffer += "<label>Gateway:</label><input type='text' name='eth_gateway' value='" + String(eth_gateway) + "'><br>";
    buffer += "<button type='submit' name='save_eth'>Save Ethernet Settings</button></form>";
    server.sendContent(buffer);
    
    buffer = "<h2>3. MQTT Settings</h2><form action='/save' method='post'>";
    buffer += "<label>Broker IP:</label><input type='text' name='mqtt_server' value='" + String(mqtt_server) + "'><br>";
    buffer += "<label>Broker Port:</label><input type='number' name='mqtt_port' value='" + String(mqtt_port) + "'><br>";
    buffer += "<label>Subscribe Topic:</label><input type='text' name='mqtt_topic' value='" + String(mqtt_topic) + "'><br>";
    buffer += "<hr><p><i>The settings below are for filtering messages from the subscribed topic.</i></p>";
    buffer += "<label>Nurse Station ID:</label><input type='text' name='ns_id' value='" + String(nurse_station_id) + "'><br>";
    buffer += "<label>Room IDs (comma separated):</label><br><textarea name='rooms' rows='4' cols='50'>";
    for (int i = 0; i < room_count; i++) {
        buffer += room_ids[i];
        if (i < room_count - 1) buffer += ",";
    }
    buffer += "</textarea><br>";
    buffer += "<button type='submit' name='save_mqtt'>Save MQTT Settings</button></form>";
    server.sendContent(buffer);
    
    buffer = "<h2>4. NTP Settings</h2><form action='/save' method='post'>";
    buffer += "<label>NTP Server:</label><input type='text' name='ntp_server' value='" + String(ntp_server) + "'><br>";
    buffer += "<button type='submit' name='save_ntp'>Save NTP & Sync</button></form>";
    server.sendContent(buffer);
    
    buffer = "<h2>5. Device Time</h2>";
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)){
        char time_buf[64];
        strftime(time_buf, sizeof(time_buf), "%A, %B %d %Y %H:%M:%S", &timeinfo);
        buffer += "<p>" + String(time_buf) + "</p>";
    } else {
        buffer += "<p>Time not synced.</p>";
    }
    server.sendContent(buffer);
    
    server.sendContent(FPSTR(HTML_FOOTER));
    server.sendContent("");
}

void handleSave() {
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    preferences.begin("flash-light", false);
    bool should_reboot = false;
    
    if (server.hasArg("save_eth")) {
        bool use_dhcp = server.arg("eth_dhcp") == "1";
        preferences.putBool("eth_dhcp_en", use_dhcp);
        if (!use_dhcp) {
            IPAddress temp_ip;
            if (!temp_ip.fromString(server.arg("eth_static_ip")) || !temp_ip.fromString(server.arg("eth_subnet")) || !temp_ip.fromString(server.arg("eth_gateway"))) {
                server.send(400, "text/html", "<h1>Error</h1><p>Invalid Static IP address format.</p><a href='/setting'>Go back</a>");
                preferences.end(); 
                return;
            }
            preferences.putString("eth_static_ip", server.arg("eth_static_ip"));
            preferences.putString("eth_subnet", server.arg("eth_subnet"));
            preferences.putString("eth_gateway", server.arg("eth_gateway"));
        }
        should_reboot = true;
    }
    
    if (server.hasArg("save_mqtt")) {
        preferences.putString("mqtt_server", server.arg("mqtt_server"));
        preferences.putInt("mqtt_port", server.arg("mqtt_port").toInt());
        preferences.putString("mqtt_topic", server.arg("mqtt_topic"));
        preferences.putString("ns_id", server.arg("ns_id"));
        String rooms_str = server.arg("rooms");
        rooms_str.replace(" ", "");
        
        int old_room_count = preferences.getInt("room_count", 0);
        for(int i=0; i < old_room_count; i++){ 
            String key = "room_" + String(i); 
            preferences.remove(key.c_str()); 
        }
        
        int count = 0;
        int last_idx = 0;
        for (int i = 0; i < rooms_str.length() && count < 60; i++) {
            if (rooms_str.charAt(i) == ',') {
                if(i > last_idx){ 
                    String key = "room_" + String(count);
                    preferences.putString(key.c_str(), rooms_str.substring(last_idx, i)); 
                    count++; 
                }
                last_idx = i + 1;
            }
        }
        if (last_idx < rooms_str.length() && count < 60) { 
            String key = "room_" + String(count);
            preferences.putString(key.c_str(), rooms_str.substring(last_idx)); 
            count++; 
        }
        preferences.putInt("room_count", count);
        should_reboot = true;
    }
    
    if (server.hasArg("save_ntp")) {
        preferences.putString("ntp_server", server.arg("ntp_server"));
        should_reboot = true;
    }
    
    preferences.end();
    
    if(should_reboot) {
        server.send(200, "text/html", "<h1>Settings Saved</h1><p>Settings saved. The device will now restart.</p><meta http-equiv='refresh' content='3;url=/'>");
        delay(3000);
        needRestart = true;
    } else {
        server.sendHeader("Location", "/setting", true);
        server.send(302, "text/plain", "");
    }
}

void handleFWUploadPage() {
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(200, "text/html", FPSTR(HTML_FIRMWARE_PAGE));
}

void handleHWTest() {
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    now_light_status = STATUS_TEST;
    if (server.hasArg("gpio")) {
        int gpio = server.arg("gpio").toInt();
        if (server.hasArg("action")) {
            if (server.arg("action") == "on") {
                digitalWrite(gpio, HIGH);
                Serial.printf("GPIO %d ON\n", gpio);
            } else {
                digitalWrite(gpio, LOW);
                Serial.printf("GPIO %d OFF\n", gpio);
            }
        }
    }
    server.send(200, "text/html", FPSTR(HTML_HW_TEST_PAGE));
}

void handleApiStatus() {
     if (!session_authenticated) {
        server.send(401, "application/json", "{\"error\":\"Not authorized\"}");
        return;
     }
     
     api_doc.clear();
     api_doc["FW"] = FIRMWARE_VERSION;
     api_doc["device lamp ID"] = ETH.macAddress();
     api_doc["Ethernet Connected"] = eth_connected;
     
     if(eth_connected) {
        api_doc["ip"] = ETH.localIP().toString();
        api_doc["Subnet"] = ETH.subnetMask().toString();
        api_doc["Gateway"] = ETH.gatewayIP().toString();
     } else {
        api_doc["ip"] = "N/A";
     }
     
     api_doc["MQTT Broker"] = String(mqtt_server);
     api_doc["MQTT port"] = mqtt_port;
     api_doc["MQTT subscribed topic"] = String(mqtt_topic);
     api_doc["nurse_station_id"] = String(nurse_station_id);
     
     JsonArray rooms = api_doc.createNestedArray("room_ids");
     for (int i = 0; i < room_count; i++) {
        rooms.add(String(room_ids[i]));
     }
     
     api_doc["NTP address"] = String(ntp_server);
     api_doc["is_lighting"] = (now_light_status != STATUS_IDLE && now_light_status != STATUS_TEST) ? 1 : 0;
     
     String status_name = "UNKNOWN";
     switch(now_light_status) {
          case STATUS_IDLE:     status_name = "IDLE"; break;
          case STATUS_SA:       status_name = "STAFF_ASSIST"; break;
          case STATUS_CF:       status_name = "Cord Fault"; break;
          case STATUS_BA:       status_name = "BATH_Assist"; break;
          case STATUS_CB_E_BE:  status_name = "CODE_BLUE_EMERGENCY"; break;
          case STATUS_E_BE:     status_name = "EMERGENCY"; break;
          case STATUS_CB:       status_name = "CODE_BLUE"; break;
          case STATUS_TEST:     status_name = "HW_TEST"; break;
     }
     api_doc["status_name"] = status_name;
     
     String output;
     serializeJson(api_doc, output);
     server.send(200, "application/json", output);
}

// ===== Main Program =====
void setup()
{
    Serial.begin(115200);
    Serial.println("\n\n--- Flash Light Booting Up ---");
    
    Serial.print("Firmware Version: ");
    Serial.println(FIRMWARE_VERSION);

    pinMode(GPIO_BUZZ, OUTPUT);
    pinMode(GPIO_LAMP_R, OUTPUT);
    pinMode(GPIO_LAMP_G, OUTPUT);
    pinMode(GPIO_LAMP_B, OUTPUT);
    pinMode(GPIO_BUTTON, OUTPUT);
    digitalWrite(GPIO_BUZZ, LOW);
    digitalWrite(GPIO_LAMP_R, LOW);
    digitalWrite(GPIO_LAMP_G, LOW);
    digitalWrite(GPIO_LAMP_B, LOW);
    digitalWrite(GPIO_BUTTON, LOW);

    for(int i = 0; i < RECORD_ALARM_POOL_SIZE; i++) { 
        alarm_list[i].callingType = -1;
    }
    
    loadSettings();
    
    #if (ENABLE_HTTPS == 1)
    Serial.println("Initializing BLE...");
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    if (pBLEScan != nullptr) {
        pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        pBLEScan->setActiveScan(true);
        pBLEScan->setInterval(100);
        pBLEScan->setWindow(99);
        Serial.println("BLE Scan Initialized Successfully.");
    } else {
        Serial.println("Error: Failed to get BLE Scan object.");
    }
    #else
    Serial.println("HTTP mode is enabled, BLE scanning is disabled.");
    #endif

    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, [](TimerHandle_t xTimer) {
        connectToMqtt();
    });
    
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_server, mqtt_port);

    timer1 = timerBegin(1, 80, true);
    timerAttachInterrupt(timer1, &buzzerModule, true);
    timerAlarmWrite(timer1, 100000, true);
    timerAlarmEnable(timer1);

    WiFi.onEvent(WiFiEvent);
    if (!eth_dhcp_enabled) {
        IPAddress local_ip, gateway, subnet;
        local_ip.fromString(eth_static_ip);
        gateway.fromString(eth_gateway);
        subnet.fromString(eth_subnet);
        ETH.config(local_ip, gateway, subnet);
        Serial.println("Attempting to connect with Static IP...");
    } else {
        Serial.println("Attempting to connect with DHCP...");
    }

    #if CONFIG_IDF_TARGET_ESP32
        if (!ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN,
                    ETH_MDIO_PIN, ETH_RESET_PIN, ETH_CLK_MODE)) {
            Serial.println("ETH start Failed!");
        }
    #else
        #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3,0,0)
            if (!ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                        SPI3_HOST,
                        ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
                Serial.println("ETH start Failed!");
            }
        #else
            if (!ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                        (spi_host_device_t)SPI3_HOST,
                        ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
                Serial.println("ETH start Failed!");
            }
        #endif
    #endif

    Serial.println("Waiting for network connection...");
    while (!eth_connected) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nNetwork connected!");

    initializeNetworkServices();
}

static unsigned long lastScanMillis = 0;
const long scanInterval = 2000;

void loop()
{
    if (network_services_initialized) {
        server.handleClient();
        
        int packetSize = Udp.parsePacket();
        if (packetSize) {
          char packetBuffer[packetSize + 1];
          Udp.read(packetBuffer, packetSize);
          packetBuffer[packetSize] = '\0';
          Serial.print("UDP Message received: "); Serial.println(packetBuffer);
          processAlarmPayload(packetBuffer);
        }
    }
    
    #if (ENABLE_HTTPS == 1)
    if (millis() - lastScanMillis >= scanInterval) {
        lastScanMillis = millis();
        if (pBLEScan != nullptr) {
            pBLEScan->start(1, false);
        }
    }
    #endif
    
    if (needRestart) {
        delay(2000);
        ESP.restart();
    }
}