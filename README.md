OwnOwl - 3D Pet 🦉
OwnOwl is a smart, AI-powered desktop companion designed to track movement and interact with users through expressive eyes and sound. It uses a dual-microcontroller architecture to handle complex AI vision tasks and smooth mechanical animations simultaneously.

📺 Video Demonstration
Watch the OwnOwl in action on YouTube:
[Buraya YouTube Linkini Yapıştır]

📥 3D Models (Creality Cloud)
Download the 3D printable parts here:
[Buraya Creality Cloud Linkini Yapıştır]

🛠 Hardware Components (BOM)
To build your own OwnOwl, you will need the following parts:

Main Brain: ESP32-S3 (Controls screens, motor, and sound)

AI Vision: ESP32-CAM (AI-Thinker module for face tracking)

Eyes: 2x Small TFT Display Screens (Compatible with LovyanGFX)

Motion: 1x Small Stepper or DC Motor (with gear system)

Sound: I2S Speaker (For "Woho" and alarm sounds)

Power: 18650 Lithium Battery & TP4056 (or similar) Charging Module

Body: 3D Printed OwnOwl chassis

📡 Connectivity & OTA
The Wi-Fi connection in this project is exclusively for Wireless Code Updates (ArduinoOTA).

The robot's AI tracking and interactions work completely offline (no internet required).

Wi-Fi allows you to flash new code to the ESP32-S3 and ESP32-CAM without opening the 3D printed body and using cables.

⚠️ Important Note: Experimental V1 Design
Please be aware that this is a Work in Progress (V1) version:

Mechanical Fit: The current 3D model may require manual sanding or cutting around the eye sockets to fit the screens perfectly.

DIY Effort: This project requires soldering, basic electronics knowledge, and manual assembly of the internal gear system.

💻 Code Structure
OWNOWL_V2.1.ino: Upload this to the ESP32-S3. It handles the eyes, sound, and mechanical tracking.

OWNOWL_Kamera.ino (previously main.cpp): Upload this to the ESP32-CAM. It handles the AI face detection.

Setup Instructions
Install ESP32 Board Manager in Arduino IDE.

Install required libraries: LovyanGFX, Stepper, and ESP32 Camera libraries.

Update Wi-Fi credentials (only if you want to use wireless updates).

Flash the boards and connect them via UART (TX/RX).
