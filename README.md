# ESP32-S3 Attendance Cam

Arduino sketch for the **Freenove ESP32-S3-WROOM** board.
Part of the ESP32-S3 + Raspberry Pi attendance system.

---

## Hardware
- Freenove ESP32-S3-WROOM (16MB Flash, 8MB PSRAM, onboard camera)

---

## How it works
1. ESP32 boots and starts a WiFi Access Point: `ESP32-Camera` / `12345678`
2. Connect your phone or Raspberry Pi to that WiFi
3. Open browser and go to `http://192.168.4.1`
4. You will see the live camera stream
5. Press **CAPTURE** to take a photo and send it to the Raspberry Pi server
6. The Pi runs face recognition and logs attendance

---

## Setup — Arduino IDE

### Step 1 — Install Arduino IDE
Download from https://www.arduino.cc/en/software

### Step 2 — Add ESP32 board support
1. Open Arduino IDE
2. Go to **File > Preferences**
3. Add this URL to Additional Boards Manager URLs:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
4. Go to **Tools > Board > Boards Manager**
5. Search for `esp32` and install **esp32 by Espressif Systems**

### Step 3 — Select the correct board
- **Tools > Board > ESP32 Arduino > ESP32S3 Dev Module**
- **Tools > PSRAM > OPI PSRAM**
- **Tools > Flash Size > 16MB**
- **Tools > Partition Scheme > Huge APP (3MB No OTA)**
- **Tools > Port > (your COM port)**

### Step 4 — Open the sketch
Open `attendance_cam/attendance_cam.ino` in Arduino IDE

### Step 5 — Upload
Click the Upload button

---

## Configuration

If your Raspberry Pi has a different IP, update this line in `attendance_cam.ino`:
```cpp
const char* PI_SERVER_URL = "http://192.168.4.2:5000/upload";
```

---

## Raspberry Pi Server
The Pi server code is in a separate repository:
https://github.com/MohamedAlaeldin/ESP32-S3-Attendance-RaspberryPi
