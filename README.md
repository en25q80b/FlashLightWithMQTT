# 🏥 Flash Light - Smart Healthcare Nurse Call Node

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-orange.svg)
![Protocol: MQTT](https://img.shields.io/badge/Protocol-MQTT-brightgreen.svg)

Flash Light is an open-source, ESP32-based smart indicator and controller designed for modern hospital Nurse Call Systems (NCS). It features modular hardware support (via W5500/LAN8720 Ethernet), real-time alarm states, and advanced networking to provide reliable patient-to-nurse signaling in critical medical environments.

## ✨ Key Features

* **Flexible Ethernet Connectivity:** Hardwired for enterprise reliability using modern ETH PHY modules (W5500, LAN8720), supporting both DHCP and Static IP.
* **Compile-Time Features (`ENABLE_HTTPS`):** * Switch easily between `HTTP-Only` or `HTTPS + BLE` modes to fit your security and memory requirements.
  * When enabled, the device scans for Bluetooth Low Energy (BLE) beacons and reports RSSI telemetry via MQTT.
* **Web-Based Provisioning:** Built-in Web UI for easy on-site installation, network configuration, MQTT topic binding, and specific room assignments.
* **Over-The-Air (OTA) Updates:** Secure firmware updates via the Web UI to ensure devices remain compliant with operational standards.
* **Complex Alarm Logic:** Handles multifaceted medical alarms (Code Blue, Staff Assist, Cord Fault, Bath Assist) with synchronized LED indicators and buzzer rhythms.
* **Hardware Diagnostic Mode:** Integrated hardware test page for deployment verification (test individual GPIOs for LEDs and buzzers).

## 🛠️ System Architecture

The firmware is designed with modularity in mind, separating hardware interrupts from network layers to maintain stability during high network traffic.

* **MCU:** ESP32 Series
* **Network:** Ethernet via PHY (W5500/LAN8720)
* **Messaging:** Async MQTT (with JSON payloads)

## 🚀 Getting Started

### Prerequisites
* [Arduino IDE](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
* ESP32 Board Support Package installed
* Required Libraries: `AsyncMQTT_ESP32`, `ArduinoJson`, `ESPWebServerSecure` (if HTTPS is used).

### Installation & Configuration

1.  **Clone the repository:**
    ```bash
    git clone [https://github.com/YourUsername/FlashLight.git](https://github.com/YourUsername/FlashLight.git)
    ```
2.  **Security Configuration (Optional):**
    * If using HTTPS mode (`#define ENABLE_HTTPS 1`), navigate to the sketch directory, copy `device_cert_template.h` to `device_cert.h`, and insert your own X.509 certificates for secure Web Server and MQTT connections.
3.  **Hardware Selection:**
    * Ensure you select the correct board profile in `utilities.h` (e.g., LILYGO_T_ETH_LITE_ESP32S3) before compiling.
4.  **Compile & Flash:**
    * Select your ESP32 board in the IDE.
    * Compile and upload the firmware.
5.  **Initial Setup:**
    * Ensure the device is connected to your Ethernet switch. 
    * Discover its IP via your DHCP server or serial monitor.
    * Log in to the Web UI (Default credentials: `admin` / `password`).
    * Configure your MQTT broker IP, Subscribe Topic, Nurse Station ID, and mapped Room IDs.

## 🤖 AI & Open Source Vision

We are actively seeking to integrate AI-driven workflows into this repository. Our roadmap includes utilizing tools like **OpenAI Codex** to:
* Automate Code Review for firmware stability.
* Auto-generate unit tests for hardware interrupt logic.
* Assist in translating and triaging multi-lingual GitHub Issues from global healthcare developers.

## 📄 License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.