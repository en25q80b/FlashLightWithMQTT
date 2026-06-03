# 🏥 DomeLight - Smart Healthcare Nurse Call Node

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-orange.svg)
![Protocol: MQTT](https://img.shields.io/badge/Protocol-MQTT-brightgreen.svg)

DomeLight is an open-source, ESP32-based smart indicator and controller designed for modern hospital Nurse Call Systems (NCS). It integrates multi-protocol communication and physical hardware controls to provide reliable, real-time patient-to-nurse signaling in critical medical environments.

## ✨ Key Features

* **Multi-Protocol Communication:** Seamlessly combines MQTT for central server telemetry and BLE for localized device configuration.
* **Web-Based Provisioning:** Built-in Web UI for easy on-site installation, network configuration, and parameter tuning without specialized software.
* **Over-The-Air (OTA) Updates:** Secure firmware updates via Wi-Fi to ensure devices remain compliant with the latest security and operational standards.
* **Real-time Alerts:** Extremely low-latency interrupt handling for emergency calls, ensuring patient safety.

## 🛠️ System Architecture

The firmware is designed with modularity in mind, separating hardware interrupts from network layers to maintain stability during high network traffic.

* **MCU:** ESP32 Series
* **Network:** Wi-Fi (802.11 b/g/n), Bluetooth Low Energy (BLE)
* **Messaging:** MQTT over TLS (Optional)

## 🚀 Getting Started

### Prerequisites
* [Arduino IDE](https://www.arduino.cc/en/software) or [PlatformIO](https://platformio.org/)
* ESP32 Board Support Package installed

### Installation & Configuration

1.  **Clone the repository:**
    ```bash
    git clone [https://github.com/YourUsername/DomeLight.git](https://github.com/YourUsername/DomeLight.git)
    ```
2.  **Security Configuration:**
    * Navigate to `src/config/`.
    * Copy `device_cert_template.h` to `device_cert.h` and insert your own X.509 certificates for secure MQTT connections.
3.  **Compile & Flash:**
    * Select your ESP32 board in the IDE.
    * Compile and upload the firmware.
4.  **Initial Setup:**
    * Upon first boot, the device will host a Wi-Fi Access Point named `DomeLight_Setup`.
    * Connect to it and navigate to `http://192.168.4.1` to configure your facility's network credentials.

## 🤖 AI & Open Source Vision

We are actively seeking to integrate AI-driven workflows into this repository. Our roadmap includes utilizing tools like **OpenAI Codex** to:
* Automate Code Review for firmware stability.
* Auto-generate unit tests for hardware interrupt logic.
* Assist in translating and triaging multi-lingual GitHub Issues from global healthcare developers.

## 📄 License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.