/********************************************************************
 * @file DomeLight.ino
 * @author Scott
 * @brief Melten Dome Light Core Firmware
 * @version 11.0.0
 * @date 2025-10-22
 * @copyright Copyright (c) 2025
 * * Main Features:
 * - Ethernet connectivity with MQTT protocol for remote control.
 * - Built-in Web Server for configuration UI and OTA updates.
 * - Controls RGB LED and external voice IC modules.
 * - Compile-time options to enable/disable HTTPS and BLE features.
 *******************************************************************/

// =================================================================
//                 !!! MODE SELECTION SWITCH !!!
// =================================================================
/** @brief Define whether to enable HTTPS secure connection (1 = Enable, 0 = Disable). Uses HTTP when disabled. */
#define ENABLE_HTTPS 1
#define BLE_ENABLED 0
// =================================================================

#include <Arduino.h>

#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
#include <ETHClass2.h>
#define ETH ETH2
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

/** @brief Define whether to compile Bluetooth features (1 = Enable, 0 = Disable). */
#if (BLE_ENABLED == 1)
// --- BLE libraries are now ALWAYS included ---
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

// Conditionally include server and certs libraries
#if (ENABLE_HTTPS == 1)
#include <ESPWebServerSecure.hpp>
#include "device_cert.h"
#include "device_key.h"
#else
#include <WebServer.h>
#endif

#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include <functional> 
#include <sys/time.h> // For gettimeofday()

// --- Firmware Version ---
#if (ENABLE_HTTPS == 1)
    #if (BLE_ENABLED == 1)
        const char* FIRMWARE_VERSION = "DL_V_1.0.0_HTTPS_BLE";
    #else
        const char* FIRMWARE_VERSION = "DL_V_1.0.0_HTTPS_NO_BLE";
    #endif
#else
    #if (BLE_ENABLED == 1)
        const char* FIRMWARE_VERSION = "DL_V_1.0.0_HTTP_BLE";
    #else
        const char* FIRMWARE_VERSION = "DL_V_1.0.0";
    #endif
#endif

// --- Device Model Definitions ---
// DL = DomeLight
// E = Ethernet
// B = Bluetooth enable
#if (BLE_ENABLED == 1)
const char* MODEL_NAME = "DL_EB_01";
#else
const char* MODEL_NAME = "DL_E_01";
#endif

// --- Web Login Variables ---
/** @brief Default credentials for web login */
const char* www_username = "Your WiFi SSID";
const char* www_password = "Your Passward";
bool session_authenticated = false;
bool needRestart = false;

// --- Network and Service Objects ---
/** @brief Flag indicating if Ethernet is connected and IP obtained */
static bool eth_connected = false;

/** @brief Declare WebServer object based on HTTPS mode */
#if (ENABLE_HTTPS == 1)
ESPWebServerSecure server(443);
#else
WebServer server(80);
#endif

/** @brief BLE scan object pointer (declared only when BLE is enabled) */
#if (BLE_ENABLED == 1)
BLEScan* pBLEScan;
#endif

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
WiFiUDP Udp;

/** @brief Object for reading/writing NVS (Non-Volatile Storage) */
Preferences preferences;

/** @brief Used for JSON parsing */
StaticJsonDocument<1024> doc;
StaticJsonDocument<1536> api_doc;
StaticJsonDocument<512> mqttJsonDoc;

/** @brief Flag indicating firmware upload errors */
bool upload_error = false;
bool network_services_initialized = false;

// ===== Configuration Variables =====
bool eth_dhcp_enabled = true;
char eth_static_ip[16];
char eth_subnet[16];
char eth_gateway[16];

// --- Hardware GPIO Pin Definitions ---
#define ETH_MISO_PIN 11
#define ETH_MOSI_PIN 12
#define ETH_SCLK_PIN 10
#define ETH_CS_PIN 9
#define ETH_INT_PIN 13
#define ETH_RST_PIN 14
#define ETH_ADDR 1

char mqtt_server[64];
int mqtt_port;
char mqtt_topic[128];

char ntp_server[64];
const int udp_port = 2269;
#define LED_OFF 0
#define LED_RED 1
#define LED_BLUE 2
#define LED_YELLOW 3
#define LED_GREEN 4
#define STATUS_IDLE 0
#define STATUS_TEST 7

//int GPIO_BUZZ = 48;
int GPIO_LAMP_R = 19;
int GPIO_LAMP_G = 3;
int GPIO_LAMP_B = 20;
int GPIO_BUTTON = 47;

int now_light_status = STATUS_IDLE;
int last_light_status = STATUS_IDLE;

hw_timer_t* timer1 = NULL;
typedef struct {
    char room[20];
    char bed[20];
    int callingType;
} record_alarm;
#define RECORD_ALARM_POOL_SIZE 60
record_alarm alarm_list[RECORD_ALARM_POOL_SIZE];

#if (BLE_ENABLED == 1)
bool ble_enabled = false; // This is the runtime toggle, loaded from NVS
#endif

// =================================================================
//               !!! NEW REQUIREMENT VARIABLES START !!!
// =================================================================
/** @brief Stores device MAC address (Format: "a1:b2:c3:d4:e5:f6") */
char device_mac_str[18];        

/** @brief Stores device MAC address without colons (Format: "a1b2c3d4e5f6") */
char device_mac_str_plain[13];  

/** @brief Stores device hostname (Format: "melten-dome-light-a1b2c3d4e5f6") */
char device_hostname[64];       
char heartbeat_topic[128];      

// --- Heartbeat Timer ---
unsigned long lastHeartbeatMillis = 0;
const long heartbeatInterval = 60000; // 60 seconds

// --- Voice Control Logic Variables ---
/** @brief Flag indicating if music/voice is active */
bool music_is_active = false;
/** @brief Current voice ringtone index to play */
int voice_ringtone_index = 0;
// =================================================================
//               !!! NEW REQUIREMENT VARIABLES END !!!
// =================================================================


#if (ENABLE_HTTPS == 1)
/**
 * @brief Validates the certificate and private key in memory for consistency.
 * @param cert_pem  Certificate string in PEM format
 * @param key_pem   Private key string in PEM format (must be unencrypted)
 * @return true     If certificate and key are valid and match
 * @return false    If invalid or mismatched
 */
bool validate_credentials(const char* cert_pem, const char* key_pem) {
    mbedtls_x509_crt cert;
    mbedtls_pk_context pkey;
    int ret;

    // Initialize required Mbed TLS components
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&pkey);
    
    // 1. Parse certificate (length + 1 to include trailing \0)
    ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *)cert_pem, strlen(cert_pem) + 1);
    if (ret != 0) {
        Serial.printf("Mbed TLS: Certificate parse failed with error -0x%x\n", -ret);
        goto cleanup;
    }

    // 2. Parse private key (assuming unencrypted, so password params are NULL, 0)
    ret = mbedtls_pk_parse_key(&pkey, (const unsigned char *)key_pem, strlen(key_pem) + 1, NULL, 0);
    if (ret != 0) {
        Serial.printf("Mbed TLS: Private key parse failed with error -0x%x\n", -ret);
        goto cleanup;
    }

    // 3. Check if certificate and private key match
    ret = mbedtls_pk_check_pair(&cert.pk, &pkey);
    if (ret != 0) {
        Serial.printf("Mbed TLS: Key pair check failed with error -0x%x\n", -ret);
        goto cleanup;
    }

    Serial.println("Mbed TLS: Certificate and key validation successful.");

cleanup:
    // Free resources
    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&pkey);

    return (ret == 0);
}
#endif

// --- **NEW** Voice Control Logic ---
bool voice_is_active = false;
int voice_sound_index = 0;
int voice_volume_level = 5; // **NEW** Variable to store current volume
unsigned long last_voice_play_ms = 0;
long voice_repeat_interval = 3500; // Default repeat interval, now mutable

// =================================================================
//                      LOGGING INTEGRATION - START
// =================================================================
const char* LOG_FILE = "/system_log.txt";
const size_t MAX_LOG_SIZE = 5 * 1024 * 1024;

String getTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        return String(buffer);
    }
    return "0000-00-00 00:00:00";
}

void logEvent(const String& message) {
    String log_entry = getTimestamp() + " - " + message + "\n";
    File file = FFat.open(LOG_FILE, "a");
    if (!file) {
        Serial.println("Failed to open log file for appending.");
        file = FFat.open(LOG_FILE, "w");
        if (!file) {
            Serial.println("Failed to create log file.");
            return;
        }
    }

    if (file.size() > MAX_LOG_SIZE) {
        file.close();
        Serial.println("Log file exceeds max size. Rotating...");
        const char* TEMP_LOG_FILE = "/log_temp.txt";
        FFat.rename(LOG_FILE, TEMP_LOG_FILE);
        File oldFile = FFat.open(TEMP_LOG_FILE, "r");
        File newFile = FFat.open(LOG_FILE, "w");
        if (oldFile && newFile) {
            size_t seek_pos = oldFile.size() / 2;
            oldFile.seek(seek_pos);
            while (oldFile.available() && oldFile.read() != '\n');
            char buffer[256];
            while (oldFile.available()) {
                size_t len = oldFile.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
                buffer[len] = '\0';
                if(len > 0) newFile.println(buffer);
            }
            newFile.print(log_entry);
        } else {
            if(newFile) newFile.print(log_entry);
        }
        if (oldFile) oldFile.close();
        if (newFile) newFile.close();
        FFat.remove(TEMP_LOG_FILE);
    } else {
        file.print(log_entry);
        file.close();
    }
    Serial.print("LOG: "); Serial.println(message);
}

void handleLogPage();
void handleLogPage() {
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(FPSTR(HTML_HEADER));
    String buffer = "<h2>System Log</h2><pre style='background-color:#f0f0f0; border:1px solid #ccc; padding:10px; max-height: 60vh; overflow-y: scroll;'>";
    server.sendContent(buffer);

    File file = FFat.open(LOG_FILE, "r");
    if (!file) {
        server.sendContent("Log file not found.");
    } else {
        // Read last 100 lines for web display
        String log_lines[100];
        int line_count = 0;
        while (file.available()) {
            log_lines[line_count % 100] = file.readStringUntil('\n');
            line_count++;
        }
        file.close();
        
        int start_index = (line_count > 100) ? line_count % 100 : 0;
        int current_count = min(line_count, 100);
        for (int i = 0; i < current_count; i++) {
            server.sendContent(log_lines[(start_index + i) % 100] + "\n");
        }
    }
    
    server.sendContent("</pre>");
    server.sendContent(FPSTR(HTML_FOOTER));
    server.sendContent("");
}
// =================================================================
//                      LOGGING INTEGRATION - END
// =================================================================

// =================================================================
//                      VOICE IC INTEGRATION - START
// =================================================================
#define SCL_PIN 15
#define SDA_PIN 16
bool voice_flag;
byte voice_j[8] = {0b10000000, 0b01000000, 0b00100000, 0b00010000, 0b00001000, 0b00000100, 0b00000010, 0b00000001};
int current_voice_calling_type = -1;
unsigned long last_voice_play_time = 0;
const long voice_play_interval = 3500;
struct VoiceSetting { int soundIndex; int volumeLevel; };

const byte volumeMap[] = {0x80, 0x83, 0x86, 0x89, 0x8b, 0x8f}; // Hardware command mappings for volume levels

/**
 * @brief Send a two-byte command to the voice IC.
 * @param Byte0 First byte of the command.
 * @param Byte1 Second byte of the command.
 */
void SendData(byte Byte0, byte Byte1) {
    int i; byte x;
    digitalWrite(SCL_PIN, LOW); digitalWrite(SDA_PIN, LOW); delay(10);
    digitalWrite(SCL_PIN, HIGH);
    delay(8); digitalWrite(SCL_PIN, LOW); delay(4);
    x = Byte0;
    for (i = 0; i < 8; i++) {
        x = Byte0 & voice_j[i];
        voice_flag = (bool)x;
        digitalWrite(SCL_PIN, HIGH); digitalWrite(SDA_PIN, voice_flag); delay(4);
        digitalWrite(SCL_PIN, LOW); delay(4);
    }
    x = Byte1;
    for (i = 0; i < 8; i++) {
        x = Byte1 & voice_j[i];
        voice_flag = (bool)x;
        digitalWrite(SCL_PIN, HIGH); digitalWrite(SDA_PIN, voice_flag); delay(4);
        digitalWrite(SCL_PIN, LOW); delay(4);
    }
    digitalWrite(SDA_PIN, LOW);
}

void initVoiceIC() {
    Serial.println("Initializing Voice IC...");
    pinMode(SCL_PIN, OUTPUT); pinMode(SDA_PIN, OUTPUT);
    SendData(0x41, 0x00);
    Serial.println("Voice IC Initialized.");
}

/**
 * @brief Play sound based on the specified ringtone index and volume.
 * @param ringtone_index Ringtone index to play (1-21).
 * @param volume_level Volume level (0-5).
 */
 void playVoiceByIndex(int ringtone_index, int volume_level) {
    if (ringtone_index > 0) {
        logEvent("Action: Playing ringtone index " + String(ringtone_index) + " at volume " + String(volume_level));
        
        int final_volume_level = constrain(volume_level, 0, 5);
        int final_ringtone_index = constrain(ringtone_index, 1, 21);
        byte volume_hex = volumeMap[final_volume_level];
        
        SendData(volume_hex, 0x00); // Set volume
        delay(50);
        SendData(0x24, final_ringtone_index); // Play sound
        last_voice_play_ms = millis();
    }
}

/** @brief Send stop playback command to the voice IC. */
void stopVoice() {
    logEvent("Action: Sending stop voice command.");
    SendData(0x43, 0x00);
    voice_is_active = false; // **NEW** Update the state
    voice_sound_index = 0;   // **NEW** Reset the index
}
// =================================================================
//                      VOICE IC INTEGRATION - END
// =================================================================

// =================================================================
//                      IWIZARD INTEGRATION - START
// =================================================================
#define IWIZARD_UDP_PORT 5180
#define IWIZARD_TCP_PORT 5160

WiFiUDP iWizardUdp;
WiFiServer iWizardTcpServer(IWIZARD_TCP_PORT);
WiFiClient iWizardTcpClient;

void printHex(const uint8_t* buffer, size_t length) {
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", buffer[i]);
    }
    Serial.println();
}

void handleIwizardRequests() {
    // --- 1. Handle UDP Broadcast Discovery ---
    int packetSize = iWizardUdp.parsePacket();
    if (packetSize > 0) {
        uint8_t packetBuffer[packetSize];
        int len = iWizardUdp.read(packetBuffer, packetSize);
        Serial.printf("[IWIZARD DEBUG] UDP Request from %s (%d bytes): ", iWizardUdp.remoteIP().toString().c_str(), len);
        printHex(packetBuffer, len);
        
        // --- CORRECTED LOGIC ---
        // Respond to ANY 10-byte packet by calculating the response.
        if (len == 10) {
            logEvent("iWizard Event: Received 10-byte discovery broadcast.");
            uint32_t sum = 0;
            for(int i=0; i<len; i++) {
                sum += packetBuffer[i];
            }
            int response_val = (sum % 103) + 10;
            String response_str = String(response_val);

            iWizardUdp.beginPacket(iWizardUdp.remoteIP(), iWizardUdp.remotePort());
            iWizardUdp.print(response_str);
            iWizardUdp.endPacket();

            Serial.printf("[IWIZARD DEBUG] Matched 10-byte packet. Calculated and sent response: '%s'\n", response_str.c_str());
        } 
        // Fallback for simple text protocol: "A"
        else if (len == 1 && packetBuffer[0] == 'A') {
            logEvent("iWizard Event: Received TEXT 'A' discovery broadcast.");
            iWizardUdp.beginPacket(iWizardUdp.remoteIP(), iWizardUdp.remotePort());
            iWizardUdp.print("75");
            iWizardUdp.endPacket();
            Serial.println("[IWIZARD DEBUG] Matched TEXT discovery packet 'A'. Responded with '75'.");
        } else {
             Serial.println("[IWIZARD DEBUG] Received unknown UDP packet, ignoring.");
        }
    }

    // --- 2. Handle TCP Command Connection ---
    if (iWizardTcpServer.hasClient()) {
        if (iWizardTcpClient && iWizardTcpClient.connected()) {
            iWizardTcpClient.stop();
        }
        iWizardTcpClient = iWizardTcpServer.available();
        if (!iWizardTcpClient) { return; }
        Serial.printf("[IWIZARD DEBUG] TCP client connected from %s\n", iWizardTcpClient.remoteIP().toString().c_str());
        logEvent("iWizard Event: TCP client connected from " + iWizardTcpClient.remoteIP().toString());
    }

    if (iWizardTcpClient && iWizardTcpClient.connected() && iWizardTcpClient.available()) {
        String command_str = iWizardTcpClient.readStringUntil('\0');
        command_str.trim();

        if (command_str.length() > 0) {
            int command = command_str.toInt();
            Serial.printf("[IWIZARD DEBUG] Received TCP command: '%s' (int: %d)\n", command_str.c_str(), command);
            String response = "";

             switch(command) {
                case 1:  response = "80"; break; // GETHTTPPORT -> Device Mode
                case 2: { // GETLAN
                    String dhcp_status = eth_dhcp_enabled ? "1" : "0";
                    String ip = eth_dhcp_enabled ? ETH.localIP().toString() : String(eth_static_ip);
                    String subnet = eth_dhcp_enabled ? ETH.subnetMask().toString() : String(eth_subnet);
                    String gateway = eth_dhcp_enabled ? ETH.gatewayIP().toString() : String(eth_gateway);
                    response = dhcp_status + "#" + ip + "#" + subnet + "#" + gateway + "#168.95.1.1#8.8.8.8"; // Default DNS
                    break;
                }
                case 3:  response = "0#0#0#0#0#0"; break; // GETWLAN (Not applicable for Ethernet version)
                case 4: { // GETNAME -> Use custom name
                    String mac = ETH.macAddress();
                    String model = MODEL_NAME;
                    mac.replace(":", "");
                    response = "Melten "+ model + mac;
                    break;
                }
                case 5: { // GETDATATIME
                    struct tm timeinfo;
                    if(getLocalTime(&timeinfo)){
                        char buffer[64];
                        strftime(buffer, sizeof(buffer), "%a %b %e %H:%M:%S UTC %Y", &timeinfo);
                        response = String(buffer);
                    } else {
                        response = "0#0#0#0#0#0";
                    }
                    break;
                }
                case 7:  response = "0#0#0"; break; // GETMODEL -> Use custom name
                case 8:  response = MODEL_NAME; break; // GETMODEL -> Use custom name                
                case 9: response = "8"; break; // GETTZ (GMT+8)
                case 10: response = "554"; break; // GETLANMAC
                case 15: response = FIRMWARE_VERSION; break; // GETVERSION
                case 16:{
                    String mac = ETH.macAddress();
                    mac.replace(":", "");
                    response = mac;
                    break; // GETLANMAC                    
                }
                case 17: {
                    String mac = ETH.macAddress();
                    mac.replace(":", "");
                    response = mac;
                    break; // GETLANMAC
                    }
                case 18: response = "0#iddns.org#"; break; // GETDDNS (disabled)
                case 20: response = "0"; break; // GETDDNS (disabled)
                case 34: response = "0"; break; // GETDDNS (disabled)
                case 99: // CV_OK (Bye)                    
                    Serial.println("[IWIZARD DEBUG] Received separate command.");
                    break;
                default:
                    Serial.println("[IWIZARD DEBUG] Received unknown TCP command.");
                    break;
            }
            
            if (response.length() > 0) {
                iWizardTcpClient.print(response);
                Serial.printf("[IWIZARD DEBUG] Sent TCP response: '%s'\n", response.c_str());
            }
        }
    }
}
// =================================================================
//                      IWIZARD INTEGRATION - END
// =================================================================

// =================================================================
//                 !!! NEW REQUIREMENT FUNCTIONS START !!!
// =================================================================

/**
 * @brief Assemble and publish heartbeat message via MQTT.
 * This function collects device status information, builds a JSON object, and publishes it.
 */
void publishHeartbeat() {
    if (!eth_connected || !mqttClient.connected()) {
        return; // Do not send if network or MQTT is disconnected
    }

    StaticJsonDocument<1024> hb_doc;

    hb_doc["dome_light_id"] = device_hostname;

    // Get millisecond-level Unix timestamp
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long long timestamp_ms = (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
    hb_doc["timestamp_ms"] = timestamp_ms;

    hb_doc["firmware_version"] = FIRMWARE_VERSION;

    // --- **MODIFIED** Heartbeat Payload ---
    hb_doc["wired_network"] = eth_connected; // Add wired network status

    hb_doc["ip"] = ETH.localIP().toString();
    hb_doc["subnet"] = ETH.subnetMask().toString();
    hb_doc["gateway"] = ETH.gatewayIP().toString();
    hb_doc["mqtt_broker"] = String(mqtt_server);
    hb_doc["mqtt_port"] = mqtt_port;
    hb_doc["mqtt_subscribed_topic"] = String(mqtt_topic);
    hb_doc["ntp_address"] = String(ntp_server);

    // Report light status based on current GPIO state
    hb_doc["red_light_enabled"] = (digitalRead(GPIO_LAMP_R) == HIGH);
    hb_doc["green_light_enabled"] = (digitalRead(GPIO_LAMP_G) == HIGH);
    hb_doc["blue_light_enabled"] = (digitalRead(GPIO_LAMP_B) == HIGH);
    hb_doc["music_enabled"] = music_is_active;

    // Dynamically report voice-related status
    if (music_is_active) {
        hb_doc["ringtone_index"] = voice_ringtone_index;
        hb_doc["volume"] = voice_volume_level;
        hb_doc["sound_repeat_interval_ms"] = voice_repeat_interval;
    } else {
        // --- **MODIFIED** Use a null JsonVariant to set null value ---
        JsonVariant null_variant; // An empty JsonVariant is null by default
        hb_doc["ringtone_index"] = null_variant;
        hb_doc["volume"] = null_variant;
        hb_doc["sound_repeat_interval_ms"] = null_variant;
    }
    
    String output;
    serializeJson(hb_doc, output);

    mqttClient.publish(heartbeat_topic, 1, false, output.c_str());
}

/**
 * @brief Process new control messages from MQTT.
 * Replaces legacy alarm state machine logic.
 * @param payload MQTT message content in JSON format.
 */
void processControlMessage(const char* payload) {
    logEvent("Processing control message: " + String(payload));
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        return;
    }

    // --- Direct LED Control ---
    digitalWrite(GPIO_LAMP_R, doc["red_light_enabled"] | false ? HIGH : LOW);
    digitalWrite(GPIO_LAMP_G, doc["green_light_enabled"] | false ? HIGH : LOW);
    digitalWrite(GPIO_LAMP_B, doc["blue_light_enabled"] | false ? HIGH : LOW);

    // --- Direct Voice/Music Control ---
    music_is_active = doc["music_enabled"] | false;

    if (music_is_active) {
        // --- Set Voice IC playback parameters ---
        voice_volume_level = doc["volume"] | 5; // Default to 5 if volume field is omitted
        voice_ringtone_index = doc["ringtone_index"] | 0; // Default to 0 (no playback) if omitted
        voice_repeat_interval = doc["sound_repeat_interval_ms"] | 3500; // Default to 3.5 seconds

        if (voice_ringtone_index > 0) {
            playVoiceByIndex(voice_ringtone_index, voice_volume_level); // Play once immediately upon receiving command
        } else {
            // Stop playback if music is enabled but no valid ringtone index is provided
            music_is_active = false; 
            stopVoice();
        }

    } else {
        // Call stop function if command requests disabling music
        stopVoice();
    }
}

// =================================================================
//                 !!! NEW REQUIREMENT FUNCTIONS END !!!
// =================================================================

// ===== Function Prototypes =====
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
#if (BLE_ENABLED == 1)
void handleSettingBLE();
#endif
void handleSave();
void handleFWUploadPage();
void handleHWTest();
void handleApiStatus();
void WiFiEvent(arduino_event_id_t event);
void GPIO_LED_SET(int color);
void exitHardwareTestMode();
void syncNTP();
void connectToMqtt();
String generateOptions(int max, int selected);
String generateVolumeOptions(int selected);
// --- API Prototypes ---
void handleApiMqttSetting();
void handleApiMemory();
void handleApiPartitions();
void handleApiUpdateCredentials();
void handleApiDownloadLog();
void handleApiNetworkSetting();
String readFile(fs::FS &fs, const char * path);
void handleIwizardRequests();
void handleApiLogin();
// --- Function declarations for credential upload ---
#if (ENABLE_HTTPS == 1)
void handleCredentialUploadPage();
void handleApiUpdateCredentials();
void handleApiUploadCredentials_OnSuccess();
void handleApiUploadCredentials_OnUpload();
#endif

#if (BLE_ENABLED == 1)
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    // =================================================================
    //               !!! MODIFICATION START !!!
    // =================================================================
    // Buffers moved from stack to static memory to prevent stack overflow in BTC_TASK.
    // These are now shared across all calls to onResult but are used sequentially,
    // which is safe in this single-threaded callback context.
    static char logBuffer[128];
    static char jsonBuffer[512];
    // =================================================================
    //               !!! MODIFICATION END !!!
    // =================================================================

    void onResult(BLEAdvertisedDevice advertisedDevice) {
        if (advertisedDevice.haveManufacturerData() == true) {
            std::string strManufacturerData = advertisedDevice.getManufacturerData();
            if (strManufacturerData.length() >= 2 && (uint8_t)strManufacturerData[0] == 0x13 && (uint8_t)strManufacturerData[1] == 0x00) {
                if (strManufacturerData.length() >= 18) {
                    uint8_t watch_id = strManufacturerData[4];
                    uint8_t action = strManufacturerData[8];
                    uint8_t battery_level = strManufacturerData[17];
                    
                    if (action == 0x02) {
                        // Using snprintf for safer string formatting.
                        snprintf(logBuffer, sizeof(logBuffer), "BLE Event: Received action 0x02 from watch ID 0x%02X. MAC: %s, RSSI: %d", 
                                watch_id, advertisedDevice.getAddress().toString().c_str(), advertisedDevice.getRSSI());
                        logEvent(logBuffer);
                    }
                    
                    int rssi = advertisedDevice.getRSSI();
                    std::string beacon_mac_str = advertisedDevice.getAddress().toString();
                    mqttJsonDoc.clear();
                    String domeLightId = ETH.macAddress();
                    mqttJsonDoc["sipNumber"] = domeLightId;
                    mqttJsonDoc["nurseStation"] = String(nurse_station_id);
                    mqttJsonDoc["room"] = domeLightId;
                    mqttJsonDoc["bed"] = domeLightId;
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
                    }
                }
            }
        }
    }
};
// =================================================================
//               !!! MODIFICATION START !!!
// =================================================================
// Initialize the static member variables outside the class definition.
char MyAdvertisedDeviceCallbacks::logBuffer[128];
char MyAdvertisedDeviceCallbacks::jsonBuffer[512];
#endif
// =================================================================
//               !!! MODIFICATION END !!!
// =================================================================

void WiFiEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START: { 
            Serial.println("ETH Started");
            // --- REQUIREMENT #1: Set Hostname ---
            // The hostname is now generated and set here.
            String mac_str = ETH.macAddress();
            mac_str.toLowerCase();
            strncpy(device_mac_str, mac_str.c_str(), sizeof(device_mac_str) - 1);
            mac_str.replace(":", "");
            strncpy(device_mac_str_plain, mac_str.c_str(), sizeof(device_mac_str_plain) - 1);
            
            snprintf(device_hostname, sizeof(device_hostname), "melten-dome-light-%s", device_mac_str_plain);
            ETH.setHostname(device_hostname);
            Serial.printf("Hostname set to: %s\n", device_hostname);
            break;
        } 

        case ARDUINO_EVENT_ETH_CONNECTED: 
            Serial.println("ETH Connected"); 
            break;
            
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("ETH MAC: ");
            Serial.print(ETH.macAddress()); Serial.print(", IPv4: "); Serial.print(ETH.localIP());
            if (ETH.fullDuplex()) { Serial.print(", FULL_DUPLEX");
            }
            Serial.print(", "); Serial.print(ETH.linkSpeed()); Serial.println("Mbps");
            eth_connected = true;
            logEvent("Network Event: Ethernet connected. IP: " + ETH.localIP().toString());
            break;
            
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            Serial.println("ETH Disconnected");
            logEvent("Network Event: Ethernet disconnected.");
            eth_connected = false; network_services_initialized = false;
            if (mqttClient.connected()) { mqttClient.disconnect();
            }
            break;
            
        case ARDUINO_EVENT_ETH_STOP: 
            Serial.println("ETH Stopped"); 
            eth_connected = false;
            network_services_initialized = false; 
            break;
            
        default: break;
    }
}
void GPIO_LED_SET(int color) {
    switch (color) {
        case LED_OFF: digitalWrite(GPIO_LAMP_R, LOW);
            digitalWrite(GPIO_LAMP_G, LOW); digitalWrite(GPIO_LAMP_B, LOW); break;
        case LED_RED: digitalWrite(GPIO_LAMP_R, HIGH); digitalWrite(GPIO_LAMP_G, LOW); digitalWrite(GPIO_LAMP_B, LOW); break;
        case LED_BLUE: digitalWrite(GPIO_LAMP_R, LOW); digitalWrite(GPIO_LAMP_G, LOW);
            digitalWrite(GPIO_LAMP_B, HIGH); break;
        case LED_YELLOW: digitalWrite(GPIO_LAMP_R, HIGH); digitalWrite(GPIO_LAMP_G, HIGH); digitalWrite(GPIO_LAMP_B, LOW); break;
        case LED_GREEN: digitalWrite(GPIO_LAMP_R, LOW); digitalWrite(GPIO_LAMP_G, HIGH); digitalWrite(GPIO_LAMP_B, LOW);
            break;
    }
}

void exitHardwareTestMode() {
    if (now_light_status == STATUS_TEST) {
        Serial.println("Exiting hardware test mode.");
        digitalWrite(GPIO_LAMP_R, LOW); digitalWrite(GPIO_LAMP_G, LOW); digitalWrite(GPIO_LAMP_B, LOW);
        digitalWrite(GPIO_BUTTON, LOW);
        now_light_status = STATUS_IDLE;
    }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
    char message[len + 1];
    strncpy(message, payload, len);
    message[len] = '\0';
    Serial.print("MQTT Message arrived ["); Serial.print(topic); Serial.print("] "); Serial.println(message);

    // --- REQUIREMENT #7 & #8: Process EVERY message using the new control logic ---
    processControlMessage(message);
}

void loadSettings() {
    Serial.println("Loading settings from NVS...");
    preferences.begin("dome-light", false);
     #if (BLE_ENABLED == 1)
    ble_enabled = preferences.getBool("ble_enabled", false);
#endif
    // --- REQUIREMENT #2: Default to DHCP ---
    eth_dhcp_enabled = preferences.getBool("eth_dhcp_en", true);
    
    preferences.getString("eth_static_ip", eth_static_ip, sizeof(eth_static_ip));
    preferences.getString("eth_subnet", eth_subnet, sizeof(eth_subnet));
    preferences.getString("eth_gateway", eth_gateway, sizeof(eth_gateway));
    
    // --- REQUIREMENT #3: Change default MQTT Broker address and port ---
    String mqtt_server_str = preferences.getString("mqtt_server", "sage-dome-light-relay");
    strncpy(mqtt_server, mqtt_server_str.c_str(), sizeof(mqtt_server) - 1);
    mqtt_server[sizeof(mqtt_server)-1] = '\0';
    mqtt_port = preferences.getInt("mqtt_port", 2345);

    // --- REQUIREMENT #6: MQTT Topic is now dynamic, so we load "" and generate it later ---
    String topic_str = preferences.getString("mqtt_topic", ""); // Default is empty
    strncpy(mqtt_topic, topic_str.c_str(), sizeof(mqtt_topic) - 1); 
    mqtt_topic[sizeof(mqtt_topic)-1] = '\0';

   
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
        if (!getLocalTime(&timeinfo, 5000)) { 
            Serial.println("Failed to obtain time.");
            logEvent("NTP Sync Failed.");
        }
        else { 
            Serial.println("Time synced successfully.");
            logEvent("NTP Sync Successful.");
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
    logEvent("MQTT Event: Connected to broker.");
    
    // --- REQUIREMENT #6: Subscribe to the dynamically generated topic ---
    // If the topic from NVS is empty, generate the default one.
    if (strlen(mqtt_topic) == 0) {
        snprintf(mqtt_topic, sizeof(mqtt_topic), "melten-dome-light/%s/rx", device_mac_str_plain);
    }
    snprintf(heartbeat_topic, sizeof(heartbeat_topic), "melten-dome-light/%s/hb", device_mac_str_plain);


    if (strlen(mqtt_topic) > 0) {
        mqttClient.subscribe(mqtt_topic, 2);
        Serial.print("Subscribing to: "); Serial.println(mqtt_topic);
    }
}
void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.println("Disconnected from MQTT.");
    logEvent("MQTT Event: Disconnected from broker.");
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
        server.on("/api/login", HTTP_POST, handleApiLogin);
        server.on("/api/status", HTTP_GET, handleApiStatus);
        #if (BLE_ENABLED == 1)
        server.on("/setting_BLE", HTTP_GET, handleSettingBLE);
        #endif
        server.on("/log", HTTP_GET, handleLogPage);

        server.on("/api/mqtt_setting", HTTP_POST, handleApiMqttSetting);
        server.on("/api/memory", HTTP_GET, handleApiMemory);
        server.on("/api/partitions", HTTP_GET, handleApiPartitions);
        
        
        server.on("/api/downloadlog", HTTP_GET, handleApiDownloadLog);
        server.on("/api/network_setting", HTTP_POST, handleApiNetworkSetting);

#if (ENABLE_HTTPS == 1)
server.on("/api/update_credentials_text", HTTP_POST, handleApiUpdateCredentials);
        server.on("/credential_upload", HTTP_GET, handleCredentialUploadPage);
        server.on("/api/upload_credentials", HTTP_POST, 
            handleApiUploadCredentials_OnSuccess, 
            handleApiUploadCredentials_OnUpload
        );
        #endif
        server.on("/api/fwupgrade", HTTP_POST, []() {
            if (!session_authenticated) { server.send(401, "application/json", "{\"error\":\"Not authorized\"}"); return; }
            logEvent("API Event: Firmware update finished.");
            server.sendHeader("Connection", "close");
            if (upload_error) { server.send(400, "application/json", "{\"status\":\"failed\", \"message\":\"Invalid file type. Please upload a .bin file.\"}"); }
            else { server.send(200, "application/json", (Update.hasError()) ? "{\"status\":\"failed\", \"message\":\"Update failed.\"}" : 
                "{\"status\":\"success\", \"message\":\"Update successful! Rebooting...\"}"); }
            delay(3000);
            if (!Update.hasError() && !upload_error) { needRestart = true; }
            upload_error = false;
        }, []() { // onUpload
            if (!session_authenticated) return;
            HTTPUpload& upload = server.upload();
         
           if (upload.status == UPLOAD_FILE_START) {
                logEvent("API Event: Firmware update started: " + upload.filename);
                upload_error = false;
                Serial.printf("API FW Update: %s\n", upload.filename.c_str());
                if (!upload.filename.endsWith(".bin")) { Serial.println("Invalid file extension."); upload_error = true; return;
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial);
                    upload_error = true; }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (upload_error) return;
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); upload_error = true;
                }
                yield();
            } else if (upload.status == UPLOAD_FILE_END) {
                if (upload_error) return;
                if (!Update.end(true)) { Update.printError(Serial); }
                else { Serial.printf("Update Success: %u\n", upload.totalSize);
                }
            }
        });
        server.on("/update", HTTP_POST, []() {
            if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
            logEvent("Firmware update finished.");
            server.sendHeader("Connection", "close");
            if (upload_error) { server.send(400, "text/plain", "INVALID FILE TYPE: Please upload a .bin file."); }
            else { server.send(200, "text/plain", (Update.hasError()) ? "UPDATE FAILED" : "UPDATE SUCCESS! Rebooting..."); }
            delay(3000);
            if (!Update.hasError() && !upload_error) { needRestart = true; }
            upload_error = false;
        }, []() { // onUpload
            if (!session_authenticated) return;
            HTTPUpload& upload = server.upload();
            
            if (upload.status == UPLOAD_FILE_START) {
                logEvent("Firmware update started: " + upload.filename);
                upload_error = false;
                Serial.printf("Update: %s\n", upload.filename.c_str());
                if (!upload.filename.endsWith(".bin")) { Serial.println("Invalid file extension."); upload_error = true; return;
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial);
                    upload_error = true; }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (upload_error) return;
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); upload_error = true;
                }
                yield();
            } else if (upload.status == UPLOAD_FILE_END) {
                if (upload_error) return;
                if (!Update.end(true)) { Update.printError(Serial); }
                else { Serial.printf("Update Success: %u\n", upload.totalSize);
                }
            }
        });
        server.onNotFound(handleNotFound);

        #if (ENABLE_HTTPS == 1)
        Serial.println("Loading SSL certificate and key for HTTPS server...");
        if (FFat.exists("/cert.pem") && FFat.exists("/key.pem")) {
            
            bool success = false;
            File certFile = FFat.open("/cert.pem", "r");
            File keyFile = FFat.open("/key.pem", "r");
            if (certFile && keyFile) {
                size_t certSize = certFile.size();
                size_t keySize = keyFile.size();

                std::unique_ptr<char[]> cert_buf(new char[certSize]);
                std::unique_ptr<char[]> key_buf(new char[keySize]);

                if (cert_buf && key_buf) {
                    if (certFile.readBytes(cert_buf.get(), certSize) == certSize &&
                        keyFile.readBytes(key_buf.get(), keySize) == keySize) {
                        
                        server.setServerKeyAndCert((const uint8_t*)key_buf.get(), keySize, (const uint8_t*)cert_buf.get(), certSize);
                        Serial.println("Loaded credentials from filesystem.");
                        success = true;
                    }
                }
            }

            if (!success) {
                Serial.println("Failed to load credentials from filesystem, using fallback.");
                server.setServerKeyAndCert((const uint8_t*)fallback_key_pem, strlen(fallback_key_pem), (const uint8_t*)fallback_cert_pem, strlen(fallback_cert_pem));
            }

            if (certFile) certFile.close();
            if (keyFile) keyFile.close();

        } else {
            server.setServerKeyAndCert((const uint8_t*)fallback_key_pem, strlen(fallback_key_pem), (const uint8_t*)fallback_cert_pem, strlen(fallback_cert_pem));
            Serial.println("No credentials on filesystem, using fallback.");
        }
        #endif

        server.begin();
        logEvent("System Event: Web server started.");
        #if (ENABLE_HTTPS == 1)
        Serial.println("HTTPS server started on port 443");
        #else
        Serial.println("HTTP server started on port 80");
        #endif

        syncNTP();
        Udp.begin(udp_port);
        Serial.printf("UDP listener started on port %d\n", udp_port);

        iWizardUdp.begin(IWIZARD_UDP_PORT);
        iWizardTcpServer.begin();
        Serial.printf("iWizard UDP listener started on port %d\n", IWIZARD_UDP_PORT);
        Serial.printf("iWizard TCP server started on port %d\n", IWIZARD_TCP_PORT);
        
        connectToMqtt();
        network_services_initialized = true;
        initVoiceIC();

    } else {
        Serial.println("Skipping network services initialization: No active connection.");
    }
}
// ===== Web Server Handlers =====
String generateOptions(int max, int selected) {
    String options = "";
    for (int i = 1; i <= max; i++) {
        options += "<option value='" + String(i) + "'";
        if (i == selected) { options += " selected"; }
        options += ">" + String(i) + "</option>";
    }
    return options;
}
String generateVolumeOptions(int selected) {
    String options = "";
    for (int i = 0; i < 6; i++) {
        options += "<option value='" + String(i) + "'";
        if (i == selected) { options += " selected"; }
        options += ">Level " + String(i) + (i == 0 ? " (Mute)" : "") + "</option>";
    }
    return options;
}
String readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);
    File file = fs.open(path, "r");
    if(!file || file.isDirectory()){
        Serial.println("- failed to open file for reading");
        return String();
    }
    String fileContent;
    while(file.available()){
        fileContent += (char)file.read();
    }
    file.close();
    return fileContent;
}
void handleRoot() { server.send(200, "text/plain", "hello from esp32!"); }
void handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: " + server.uri();
    message += "\nMethod: " + String((server.method() == HTTP_GET) ? "GET" : "POST");
    message += "\nArguments: " + String(server.args()) + "\n";
    for (uint8_t i = 0; i < server.args(); i++) { message += " " + server.argName(i) + ": " + server.arg(i) + "\n"; }
    server.send(404, "text/plain", message);
}
void handleLoginPage() {
    String login_page = FPSTR(HTML_LOGIN_PAGE);
    login_page.replace("%ERROR%", server.hasArg("error") ? "Invalid username or password." : "");
    server.send(200, "text/html", login_page);
}
void handleDoLogin() {
    if (server.hasArg("username") && server.hasArg("password") && server.arg("username") == String(www_username) && server.arg("password") == String(www_password)) {
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
/**
 * @brief Handles API login requests.
 * Responds with JSON instead of redirecting.
 */
void handleApiLogin() {
    if (server.hasArg("username") && server.hasArg("password") && 
        server.arg("username") == String(www_username) && 
        server.arg("password") == String(www_password)) {
        
        session_authenticated = true;
        logEvent("API Event: Login successful via /api/login.");
        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Login successful\"}");

    } else {
        session_authenticated = false;
        logEvent("API Event: Login failed via /api/login.");
        server.send(401, "application/json", "{\"status\":\"failed\", \"message\":\"Invalid credentials\"}");
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
    if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(FPSTR(HTML_HEADER));
    String buffer; buffer.reserve(4096);
    buffer = "<h3>Current Firmware Version: " + String(FIRMWARE_VERSION) + "</h3>";
    server.sendContent(buffer);
    
    buffer = "<h2>1. Mode Information</h2>";
    #if (ENABLE_HTTPS == 1)
    buffer += "<p><b>Current Mode:</b> HTTPS</p>";
    #else
    buffer += "<p><b>Current Mode:</b> HTTP</p>";
    #endif
#if (BLE_ENABLED == 1)
    buffer += "<p><b>Bluetooth Status:</b> " + String(ble_enabled ? "ENABLED" : "DISABLED") + "</p>";
    buffer += "<p>To change BLE status, please go to the hidden /setting_BLE page.</p>";
    server.sendContent(buffer);
#endif

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
    buffer += "<p><b>Current Subscribe Topic:</b> " + String(mqtt_topic) + "</p>";
    buffer += "<p><i>Note: The subscribe topic is now dynamically generated based on the device MAC address (melten-dome-light/&lt;mac&gt;/rx).</i></p>";
    buffer += "<p><b>Heartbeat Topic:</b> " + String(heartbeat_topic) + " </p>"; // Display heartbeat topic
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
#if (BLE_ENABLED == 1)
void handleSettingBLE() {
    if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(FPSTR(HTML_HEADER));

    String buffer; buffer.reserve(1024);
    buffer = "<h3>Current Firmware Version: " + String(FIRMWARE_VERSION) + "</h3>";
    server.sendContent(buffer);

    buffer = "<h2>Bluetooth (BLE) Settings</h2>";
    buffer += "<p>Enable or disable the Bluetooth scanning function. <b>A restart is required for changes to take effect.</b></p>";
    buffer += "<p>Current Status: <b>" + String(ble_enabled ? "ENABLED" : "DISABLED") + "</b></p>";
    buffer += "<form action='/save' method='post'>";
    buffer += "<label>Enable Bluetooth</label><input type='radio' name='ble_enabled' value='1' " + String(ble_enabled ? "checked" : "") + "><br>";
    buffer += "<label>Disable Bluetooth</label><input type='radio' name='ble_enabled' value='0' " + String(ble_enabled ? "" : "checked") + "><br><br>";
    buffer += "<button type='submit' name='save_ble'>Save and Reboot</button></form>";
    server.sendContent(buffer);
    
    server.sendContent(FPSTR(HTML_FOOTER));
    server.sendContent("");
}
#endif
void handleSave() {
    if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
    preferences.begin("dome-light", false);
    bool should_reboot = false;
    
#if (BLE_ENABLED == 1)
    if (server.hasArg("save_ble")) {
        bool use_ble = server.arg("ble_enabled") == "1";
        preferences.putBool("ble_enabled", use_ble);
        logEvent("Settings Change: BLE " + String(use_ble ? "enabled" : "disabled") + ". Rebooting.");
        should_reboot = true;
    }
    #endif
    
    if (server.hasArg("save_eth")) {
        logEvent("Settings Change: Saving Ethernet settings.");
        bool use_dhcp = server.arg("eth_dhcp") == "1";
        preferences.putBool("eth_dhcp_en", use_dhcp);
        if (!use_dhcp) {
            IPAddress temp_ip;
            if (!temp_ip.fromString(server.arg("eth_static_ip")) || !temp_ip.fromString(server.arg("eth_subnet")) || !temp_ip.fromString(server.arg("eth_gateway"))) {
                server.send(400, "text/html", "<h1>Error</h1><p>Invalid Static IP address format.</p><a href='/setting'>Go back</a>");
                preferences.end(); return;
            }
            preferences.putString("eth_static_ip", server.arg("eth_static_ip"));
            preferences.putString("eth_subnet", server.arg("eth_subnet"));
            preferences.putString("eth_gateway", server.arg("eth_gateway"));
        }
        should_reboot = true;
    }
    if (server.hasArg("save_mqtt")) {
        logEvent("Settings Change: Saving MQTT settings.");
        preferences.putString("mqtt_server", server.arg("mqtt_server"));
        preferences.putInt("mqtt_port", server.arg("mqtt_port").toInt());
        preferences.putString("mqtt_topic", server.arg("mqtt_topic"));
        should_reboot = true;
    }
    if (server.hasArg("save_ntp")) {
        logEvent("Settings Change: Saving NTP settings.");
        preferences.putString("ntp_server", server.arg("ntp_server"));
        should_reboot = true;
    }


    preferences.end();
    if(should_reboot) {
        logEvent("System Event: Rebooting due to settings change...");
        server.send(200, "text/html", "<h1>Settings Saved</h1><p>Settings saved. The device will now restart.</p><meta http-equiv='refresh' content='3;url=/'>");
        delay(3000);
        needRestart = true;
    } else {
        server.sendHeader("Location", "/setting", true);
        server.send(302, "text/plain", "");
    }
}
void handleFWUploadPage() {
    if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
    server.send(200, "text/html", FPSTR(HTML_FIRMWARE_PAGE));
}
void handleHWTest() {
    if (!session_authenticated) { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); return; }
    now_light_status = STATUS_TEST;
    if (server.hasArg("gpio")) {
        int gpio = server.arg("gpio").toInt();
        if (server.hasArg("action")) {
            if (server.arg("action") == "on") { digitalWrite(gpio, HIGH); Serial.printf("GPIO %d ON\n", gpio); }
            else { digitalWrite(gpio, LOW); Serial.printf("GPIO %d OFF\n", gpio); }
        }
    }
    if (server.hasArg("play_ringtone")) {
        if (server.hasArg("sound_index") && server.hasArg("volume_level")) {
            int sound_index = constrain(server.arg("sound_index").toInt(), 1, 21);
            int volume_level = constrain(server.arg("volume_level").toInt(), 0, 5);
            Serial.printf("Hardware Test: Playing sound index %d at volume level %d\n", sound_index, volume_level);
            byte volume_hex = volumeMap[volume_level];
            SendData(volume_hex, 0x00);
            delay(50);
            SendData(0x24, sound_index);
        }
    }
    server.send(200, "text/html", FPSTR(HTML_HW_TEST_PAGE));
}
void handleApiStatus() {
    if (!session_authenticated) { server.send(401, "application/json", "{\"error\":\"Not authorized\"}"); return; }
    
    api_doc.clear();
    
    api_doc["FW"] = FIRMWARE_VERSION;
    api_doc["device_lamp_id"] = ETH.macAddress();
    api_doc["ethernet_connected"] = eth_connected;
    
    if (eth_connected) {
        api_doc["ip"] = ETH.localIP().toString();
        api_doc["subnet"] = ETH.subnetMask().toString();
        api_doc["gateway"] = ETH.gatewayIP().toString();
    } else {
        api_doc["ip"] = "N/A";
    }
    
    api_doc["mqtt_broker"] = String(mqtt_server);
    api_doc["mqtt_port"] = mqtt_port;
    api_doc["mqtt_subscribed_topic"] = String(mqtt_topic);
    api_doc["ntp_address"] = String(ntp_server);
    
    // --- NEW STATUS FIELDS ---
    api_doc["red_light_enabled"] = (digitalRead(GPIO_LAMP_R) == HIGH);
    api_doc["green_light_enabled"] = (digitalRead(GPIO_LAMP_G) == HIGH);
    api_doc["blue_light_enabled"] = (digitalRead(GPIO_LAMP_B) == HIGH);
    api_doc["music_enabled"] = music_is_active;

    if (music_is_active) {
        api_doc["ringtone_index"] = voice_ringtone_index;
        api_doc["sound_repeat_interval_ms"] = voice_repeat_interval;
        api_doc["volume"] = voice_volume_level;
    } else {
        JsonVariant null_variant;
        api_doc["ringtone_index"] = null_variant;
        api_doc["sound_repeat_interval_ms"] = null_variant;
        api_doc["volume"] = null_variant;
    }
    
    String output;
    serializeJson(api_doc, output);
    server.send(200, "application/json", output);
}
void handleApiMemory() {
    if (!session_authenticated) {
        server.send(401, "application/json", "{\"error\":\"Not authorized\"}");
        return;
    }
    api_doc.clear();
    api_doc["heap_total_bytes"] = ESP.getHeapSize();
    api_doc["heap_free_bytes"] = ESP.getFreeHeap();
    api_doc["heap_used_bytes"] = ESP.getHeapSize() - ESP.getFreeHeap();
    api_doc["heap_min_free_bytes"] = ESP.getMinFreeHeap();
    api_doc["heap_used_percent"] = (1.0 - (float)ESP.getFreeHeap() / (float)ESP.getHeapSize()) * 100.0;
    api_doc["fs_total_bytes"] = FFat.totalBytes();
    api_doc["fs_used_bytes"] = FFat.usedBytes();
    api_doc["fs_free_bytes"] = FFat.totalBytes() - FFat.usedBytes();
    if (FFat.totalBytes() > 0) {
      api_doc["fs_used_percent"] = ((float)FFat.usedBytes() / (float)FFat.totalBytes()) * 100.0;
    } else {
      api_doc["fs_used_percent"] = 0;
    }
    String output;
    serializeJson(api_doc, output);
    server.send(200, "application/json", output);
}
void handleApiPartitions() {
    if (!session_authenticated) {
        server.send(401, "application/json", "{\"error\":\"Not authorized\"}");
        return;
    }
    api_doc.clear();
    JsonArray partitions = api_doc.createNestedArray("partitions");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (!it) {
        server.send(500, "application/json", "{\"error\":\"Could not find partitions\"}");
        return;
    }
    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p) {
            JsonObject partition = partitions.createNestedObject();
            partition["type"] = p->type;
            partition["subtype"] = p->subtype;
            partition["label"] = p->label;
            partition["address"] = "0x" + String(p->address, HEX);
            partition["size"] = p->size;
            partition["encrypted"] = p->encrypted;
        }
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    String output;
    serializeJson(api_doc, output);
    server.send(200, "application/json", output);
}
void handleApiMqttSetting() {
    if (!session_authenticated) { server.send(401, "application/json", "{\"error\":\"Not authorized\"}"); return; }
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
    
    String body = server.arg("plain");
    DeserializationError error = deserializeJson(doc, body);
    if (error) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }
    
    if (!doc.containsKey("MQTT Broker") || !doc.containsKey("MQTT port")) {
        server.send(400, "application/json", "{\"error\":\"Missing required fields: MQTT Broker, MQTT port\"}");
        return;
    }
    preferences.begin("dome-light", false);
    preferences.putString("mqtt_server", doc["MQTT Broker"].as<String>());
    preferences.putInt("mqtt_port", doc["MQTT port"].as<int>());    
    preferences.end();
    logEvent("API Event: MQTT settings updated. Rebooting.");
    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Settings saved. Device will restart.\"}");
    delay(3000);
    needRestart = true;
}
void handleApiNetworkSetting() {
    if (!session_authenticated) { server.send(401, "application/json", "{\"error\":\"Not authorized\"}"); return; }
    if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

    String body = server.arg("plain");
    DeserializationError error = deserializeJson(doc, body);
    if (error) { server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}"); return; }

    if (!doc.containsKey("IP_type")) { server.send(400, "application/json", "{\"error\":\"Missing required field: IP_type\"}"); return; }

    String ip_type = doc["IP_type"];
    bool is_static = (ip_type.equalsIgnoreCase("static"));

    preferences.begin("dome-light", false);
    
    if (is_static) {
        if (!doc.containsKey("ip_address") || !doc.containsKey("subnet") || !doc.containsKey("gateway")) {
            preferences.end();
            server.send(400, "application/json", "{\"error\":\"Missing required fields for static IP: ip_address, subnet, gateway\"}");
            return;
        }
        IPAddress temp_ip;
        if (!temp_ip.fromString(doc["ip_address"].as<const char*>()) || !temp_ip.fromString(doc["subnet"].as<const char*>()) || !temp_ip.fromString(doc["gateway"].as<const char*>())) {
            preferences.end();
            server.send(400, "application/json", "{\"error\":\"Invalid IP address format\"}");
            return;
        }
        preferences.putBool("eth_dhcp_en", false);
        preferences.putString("eth_static_ip", doc["ip_address"].as<String>());
        preferences.putString("eth_subnet", doc["subnet"].as<String>());
        preferences.putString("eth_gateway", doc["gateway"].as<String>());
    } else { // DHCP
        preferences.putBool("eth_dhcp_en", true);
    }

    preferences.end();
    logEvent("API Event: Network settings updated. Rebooting.");
    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Settings saved. Device will restart.\"}");
    delay(3000);
    needRestart = true;
}

// Legacy API handlers (fallback logic)
#if (ENABLE_HTTPS == 1)
void handleApiUpdateCredentials() {

    if (!session_authenticated) { server.send(401, "application/json", "{\"error\":\"Not authorized\"}"); return; }
    if (!server.hasArg("certificate") || !server.hasArg("private_key")) {
        server.send(400, "application/json", "{\"error\":\"Missing certificate or private_key arguments\"}");
        return;
    }
    
    String cert_str_raw = server.arg("certificate");
    String key_str_raw = server.arg("private_key");

    String cert_str = cert_str_raw;
    cert_str.replace("\r", "");
    String key_str = key_str_raw;
    key_str.replace("\r", "");

    if (!validate_credentials(cert_str.c_str(), key_str.c_str())) {
        logEvent("API Event: Failed to update credentials due to validation error.");
        server.send(400, "application/json", "{\"error\":\"Invalid certificate or key. Could not parse or verify key pair.\"}");
        return; 
    }

    logEvent("API Event: New credentials validated successfully. Updating...");
    Serial.println("Writing new credentials to filesystem...");
    
    File certFile = FFat.open("/cert.pem", "w");
    if (!certFile || certFile.print(cert_str) == 0) {
        server.send(500, "application/json", "{\"error\":\"Failed to write certificate file\"}");
        if(certFile) certFile.close();
        Serial.println("Error writing certificate file.");
        return;
    }
    certFile.close();

    File keyFile = FFat.open("/key.pem", "w");
    if (!keyFile || keyFile.print(key_str) == 0) {
        server.send(500, "application/json", "{\"error\":\"Failed to write private key file\"}");
        if(keyFile) keyFile.close();
        Serial.println("Error writing private key file.");
        return;
    }
    keyFile.close();

    Serial.println("Credentials updated successfully. Triggering restart.");
    server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Credentials updated. Device will restart.\"}");
    delay(2000);
    needRestart = true;
}

#endif

void handleApiDownloadLog() {
    if (!session_authenticated) {
        server.send(401, "application/json", "{\"error\":\"Not authorized\"}");
        return;
    }
    File logFile = FFat.open(LOG_FILE, "r");
    if (!logFile) {
        server.send(404, "text/plain", "Log file not found.");
        return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"dome_light_log.txt\"");
    server.streamFile(logFile, "text/plain");
    logFile.close();
    logEvent("API Event: System log downloaded.");
}

// ===== Function declarations for credential upload =====
#if (ENABLE_HTTPS == 1)
void handleCredentialUploadPage() {
    if (!session_authenticated) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(200, "text/html", FPSTR(HTML_CREDENTIAL_UPLOAD_PAGE));
}

// No parameters, internally calls server.upload()
void handleApiUploadCredentials_OnUpload() {
    if (!session_authenticated) return;

    HTTPUpload& upload = server.upload();
    String tempPath = "";

    if (upload.name == "cert_file") {
        tempPath = "/cert.tmp";
    } else if (upload.name == "key_file") {
        tempPath = "/key.tmp";
    } else {
        return; 
    }

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Receiving file: %s for field: %s\n", upload.filename.c_str(), upload.name.c_str());
        File tempFile = FFat.open(tempPath, "w");
        if (!tempFile) {
            Serial.println("Failed to open temp file for writing");
        }
        tempFile.close();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        File tempFile = FFat.open(tempPath, "a");
        if (tempFile) {
            tempFile.write(upload.buf, upload.currentSize);
            tempFile.close();
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("Finished receiving file: %s\n", upload.filename.c_str());
    }
}

void handleApiUploadCredentials_OnSuccess() {
    if (!session_authenticated) {
        server.send(401, "application/json", "{\"error\":\"Not authorized\"}");
        return;
    }

    logEvent("API Event: Credential upload finished. Starting validation.");

    String certContent = readFile(FFat, "/cert.tmp");
    String keyContent = readFile(FFat, "/key.tmp");
    
    // Always delete temp files
    FFat.remove("/cert.tmp");
    FFat.remove("/key.tmp");

    if (certContent.length() == 0 || keyContent.length() == 0) {
        logEvent("API Event: Credential validation failed. Temp files were empty or could not be read.");
        server.send(400, "application/json", "{\"error\":\"Certificate or Key file upload failed or files were empty.\"}");
        return;
    }

    if (validate_credentials(certContent.c_str(), keyContent.c_str())) {
        logEvent("API Event: Uploaded credentials validated successfully. Applying changes.");
        Serial.println("Validation successful. Writing new credentials.");

        if (FFat.exists("/cert.pem")) FFat.remove("/cert.pem");
        if (FFat.exists("/key.pem")) FFat.remove("/key.pem");

        File certFile = FFat.open("/cert.pem", "w");
        certFile.print(certContent);
        certFile.close();

        File keyFile = FFat.open("/key.pem", "w");
        keyFile.print(keyContent);
        keyFile.close();

        server.send(200, "application/json", "{\"status\":\"success\", \"message\":\"Credentials uploaded and validated. Device will restart.\"}");
        delay(2000);
        needRestart = true;
    } else {
        logEvent("API Event: Credential validation failed. Discarding uploaded files.");
        Serial.println("Validation failed. Discarding uploaded files.");
        server.send(400, "application/json", "{\"error\":\"Invalid certificate or key pair. Validation failed.\"}");
    }
}
#endif

// ===== Main Program =====
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n--- Dome Light Booting Up ---");
    if (!FFat.begin(true)) {
        Serial.println("An Error has occurred while mounting FFat");
    }

    logEvent("System Event: Device Booting. Firmware: " + String(FIRMWARE_VERSION));

    pinMode(GPIO_LAMP_R, OUTPUT);
    pinMode(GPIO_LAMP_G, OUTPUT);
    pinMode(GPIO_LAMP_B, OUTPUT);
    pinMode(GPIO_BUTTON, OUTPUT);
    digitalWrite(GPIO_LAMP_R, LOW);
    digitalWrite(GPIO_LAMP_G, LOW);
    digitalWrite(GPIO_LAMP_B, LOW);
    digitalWrite(GPIO_BUTTON, LOW);
    
    // The old alarm list is no longer used for control, but is kept for data structure reference
    for (int i = 0; i < RECORD_ALARM_POOL_SIZE; i++) {
        alarm_list[i].callingType = -1;
    }

    // --- Network initialization and MAC address acquisition advanced ---
    WiFi.onEvent(WiFiEvent); // Register event callback

    if (!eth_dhcp_enabled) { // Load network settings first, without configuring IP yet
        preferences.begin("dome-light", false);
        eth_dhcp_enabled = preferences.getBool("eth_dhcp_en", true);
        preferences.getString("eth_static_ip", eth_static_ip, sizeof(eth_static_ip));
        preferences.getString("eth_subnet", eth_subnet, sizeof(eth_subnet));
        preferences.getString("eth_gateway", eth_gateway, sizeof(eth_gateway));
        preferences.end();
    }
    
    #if CONFIG_IDF_TARGET_ESP32
        // ESP32 Native ETH - Assuming MAC is available after begin
        if (!ETH.begin(ETH_TYPE, ETH_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_RESET_PIN, ETH_CLK_MODE)) {
             Serial.println("ETH start Failed!");
        }
    #else
        // W5500 via SPI
        #if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 0, 0)
            if (!ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, SPI3_HOST, ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
                 Serial.println("ETH start Failed!");
            }
        #else
            if (!ETH.begin(ETH_PHY_W5500, ETH_ADDR, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN, (spi_host_device_t)SPI3_HOST, ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN)) {
                 Serial.println("ETH start Failed!");
            }
        #endif
    #endif

    // --- Immediately acquire MAC and configure relative strings post ETH.begin() ---
    String mac_str = ETH.macAddress();
    mac_str.toLowerCase();
    strncpy(device_mac_str, mac_str.c_str(), sizeof(device_mac_str) - 1);
    device_mac_str[sizeof(device_mac_str) - 1] = '\0'; // Ensure null termination
    mac_str.replace(":", "");
    strncpy(device_mac_str_plain, mac_str.c_str(), sizeof(device_mac_str_plain) - 1);
    device_mac_str_plain[sizeof(device_mac_str_plain) - 1] = '\0'; // Ensure null termination
    snprintf(device_hostname, sizeof(device_hostname), "melten-dome-light-%s", device_mac_str_plain);
    ETH.setHostname(device_hostname); // Set Hostname
    Serial.printf("MAC Address: %s\n", device_mac_str);
    Serial.printf("Plain MAC: %s\n", device_mac_str_plain);
    Serial.printf("Hostname set to: %s\n", device_hostname);

    // --- Load remaining settings now ---
    loadSettings(); // Loads MQTT Server/Port, NTP Server, and saved mqtt_topic

    // --- Fix: Confirm final mqtt_topic and heartbeat_topic here ---
    if (strlen(mqtt_topic) == 0) { // If loaded topic is empty
        snprintf(mqtt_topic, sizeof(mqtt_topic), "melten-dome-light/%s/rx", device_mac_str_plain);
        Serial.println("MQTT Subscribe Topic is empty in NVS, using default dynamic topic.");
    } else {
         Serial.println("Using custom MQTT Subscribe Topic from NVS.");
    }
    snprintf(heartbeat_topic, sizeof(heartbeat_topic), "melten-dome-light/%s/hb", device_mac_str_plain);
    Serial.printf("MQTT Subscribe Topic configured: %s\n", mqtt_topic);
    Serial.printf("MQTT Heartbeat Topic configured: %s\n", heartbeat_topic);
    // --- Fix End ---

    #if (BLE_ENABLED == 1)
    if (ble_enabled) {
        Serial.println("Configuration enables BLE. Initializing...");
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
    } else {
        Serial.println("Configuration disables BLE. Skipping initialization.");
    }
    #endif
    
    mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, [](TimerHandle_t xTimer) { connectToMqtt(); });
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setClientId(device_hostname); // Apply configured Hostname as Client ID
    Serial.printf("MQTT Client ID set to: %s\n", device_hostname);

    // --- Configure network IP (DHCP or Static) here ---
    if (!eth_dhcp_enabled) {
        IPAddress local_ip, gateway, subnet;
        local_ip.fromString(eth_static_ip); gateway.fromString(eth_gateway); subnet.fromString(eth_subnet);
        if (!ETH.config(local_ip, gateway, subnet)) {
             Serial.println("Failed to configure ETH static IP");
        } else {
             Serial.println("Configuring Static IP...");
        }
    } else {
         Serial.println("Configuring DHCP...");
         // For DHCP, explicit config is usually not required; begin() handles it
    }

    Serial.println("Waiting for network connection...");
    unsigned long startWait = millis();
    while (!eth_connected) {
        delay(500);
        Serial.print(".");
        if(millis() - startWait > 30000) {
            logEvent("Network Event: Connection failed (timeout). Rebooting...");
            Serial.println("\nNetwork connection failed. Restarting...");
            delay(1000); ESP.restart();
        }
    }
    Serial.println("\nNetwork connected!");

    initializeNetworkServices(); // Safe to initialize network services now
}

static unsigned long lastScanMillis = 0;
const long scanInterval = 2000;

void loop() {
    if (network_services_initialized) {
        server.handleClient();
        // The old UDP logic is kept, but it now calls the deprecated processAlarmPayload
        int packetSize = Udp.parsePacket();
        if (packetSize) {
            char packetBuffer[packetSize + 1];
            Udp.read(packetBuffer, packetSize);
            packetBuffer[packetSize] = '\0';
            Serial.print("UDP Message received: "); Serial.println(packetBuffer);
        }
        
        handleIwizardRequests();
    }

#if (BLE_ENABLED == 1)
    if (ble_enabled && (millis() - lastScanMillis >= scanInterval)) {
        lastScanMillis = millis();
        if (pBLEScan != nullptr ) {
            pBLEScan->start(1, false);
        }
    }
    #endif

    // --- Voice Repeating Logic ---
    if (music_is_active && voice_ringtone_index > 0) {
        if (millis() - last_voice_play_ms >= voice_repeat_interval) {
            playVoiceByIndex(voice_ringtone_index, voice_volume_level);
        }
    }
    
    // --- REQUIREMENT #5: Send heartbeat once a minute ---
    if (millis() - lastHeartbeatMillis >= heartbeatInterval) {
        lastHeartbeatMillis = millis();
        publishHeartbeat();
    }

    if (needRestart) {
        logEvent("System Event: Rebooting...");
        delay(2000);
        ESP.restart();
    }
}