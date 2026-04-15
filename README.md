# Cyber Owl - ESP32-CAM AI Face Tracking Robot 🦉

## Overview
Cyber Owl is an interactive, AI-powered desktop companion designed to track your face. This repository contains the Arduino code (ESP32-CAM) and hardware details for the project.

The system uses an ESP32-CAM hidden in the owl's beak. It continuously scans for faces using AI algorithms and controls a motorized gear system to rotate the head, following the person left or right.

## 📺 Video Demonstration
Watch the Cyber Owl in action on YouTube:
[https://youtube.com/shorts/lT2aZSD9aYQ?feature=share]

## 📥 3D Models (Creality Cloud)
You can download the STL files for the 3D printed parts on my Creality Cloud profile:
[Insert Creality Cloud Link Here]

## Features
* **AI Face Tracking:** Utilizes human face detection algorithms (MSR01/MNP01) to calculate face coordinates.
* **Motorized Movement:** Directs a motor (Left/Right/Center) based on the face's X-axis position.
* **OTA Support:** Includes ArduinoOTA for wireless code updates.

## Hardware Components (BOM)
* ESP32-CAM (AI-Thinker module)
* 18650 Battery & Charge Module
* Small DC/Stepper Motor
* 2x Small TFT Display Screens (for eyes)
* Speaker (for sound effects)

## ⚠️ Important Note on 3D Models (WIP / Experimental)
If you are planning to 3D print the body, please note that this is a **V1 Experimental** design. 
The current 3D model does not perfectly fit the eye screens. You will likely need to manually cut/sand the edges of the eye sockets or remix the STL files to make your specific screens fit. 

## Code Setup
1. Open the `.ino` file in the Arduino IDE.
2. Install the required ESP32 board managers and camera libraries.
3. Update the Wi-Fi credentials in the code (If using OTA updates):
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";

4c Flash the code to your ESP32-CAM board.

Feel free to fork, remix, and improve the project!
