# OwnOwl - 3D Printed Pet 🦉

OwnOwl is an interactive, AI-powered desktop companion. It features a dual-microcontroller system designed to handle real-time AI vision and smooth mechanical animations.

## 📺 Demo & 🎥 Video
Watch OwnOwl in action on YouTube:
[](https://youtube.com/shorts/lT2aZSD9aYQ?feature=share)

## 📥 Get the 3D Files
Download the STL models from Creality Cloud:
[](https://www.crealitycloud.com/flowprint/model-detail/69dfc804043ef052a5274bd5)

## 🛠 Hardware List (BOM)
* **Main Controller:** ESP32-S3 (Handles screens, motor, and sound)
* **Vision Module:** ESP32-CAM (Handles AI face tracking)
* **Displays:** 2x Small TFT Screens (ST7789 or compatible)
* **Motor:** Small DC/Stepper motor for head rotation
* **Audio:** I2S Speaker
* **Power:** 18650 Battery + Charging Module

## 📡 Connectivity & Wireless Updates (OTA)
The Wi-Fi feature in this project is used **only for Wireless Code Updates (OTA)**. 
* The robot operates **100% offline** for its AI tracking and interactions.
* Use the Wi-Fi credentials in the code to update the firmware without needing a USB cable.

## ⚠️ Known Issues (V1 Experimental)
Please note that this is a **V1 design**:
* The eye sockets in the 3D model may require manual adjustment (sanding/cutting) to fit your specific screens.
* Assembly requires basic knowledge of UART communication between two ESP32 modules.

## 💻 Software Setup
1. **`OWNOWL_V2.1.ino`**: Flash this to the **ESP32-S3** (The Brain).
2. **`OWNOWL_Kamera.ino`**: Flash this to the **ESP32-CAM** (The Eyes).

Ensure both modules are connected via UART (TX -> RX) to allow the camera to send coordinate data (F:x,y,w,h) to the main controller.

Feel free to contribute, report bugs, or suggest improvements!
