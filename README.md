# ESP8266 Single Cylinder ECU

A DIY Electronic Control Unit (ECU) for single-cylinder engines using the ESP8266. 
Great replacement for old points ignition as long as you do the necessary modifications to the flywheel. 

**NOTE:** Currently only supports points ignition. (Honda C50 style)

## Features
* **Wireless Tuning:** Access point `Moto_Tuning_Port` (Password: `1234`).
* **Live Web Dashboard:** Real-time RPM and timing display via WebSockets.
* **Ignition Control:** Adjustable advance, rev limiter, and launch control.
* **Engine Lock:** Software immobilizer via the web UI.
* **Ride Mode:** Automatically turns off Wi-Fi when the engine is running to save CPU power.

## Hardware Design
The PCB was designed in **KiCad 9.0** and is located in the `ECU_design` folder.
* **Microcontroller:** NodeMCU ESP8266 (NOT Olimex MOD-WIFI. its just a placeholder in the schematic)
* **Sensor Input:** GPIO 12 (VR Sensor)
* **Ignition Output:** GPIO 14 (Coil Trigger)
* **Power:** 6V

## Software Setup
1. Open `ecu_code.ino` in the Arduino IDE.
2. Install libraries: `ESP8266WiFi`, `ESP8266WebServer`, `WebSocketsServer`, and `LittleFS`.
3. Use the **LittleFS Upload Tool** to flash the `data` folder (containing `index.html`) to the chip.
4. Upload the sketch to the ESP8266.
5. Connect to the Wi-Fi and go to `192.168.4.1` in your browser.

## File Structure
* `/ecu_code`: Arduino sketch and Web UI files.
* `/ECU_design`: KiCad schematic and PCB layout.

## Disclaimer
Experimental project. Use at your own risk. Not fully completed
