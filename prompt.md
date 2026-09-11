Act as an expert Embedded Systems Developer specializing in ESP32, IoT, and C++ (Arduino/PlatformIO framework).

Please create a complete, modular, and production-ready ESP32 firmware for the "Ultimate Romantic Couple Lamp". I will handle the physical wiring based on the pinout definitions below.

### 1. HARDWARE PINOUT MAPPING
- ESP32 Dev Board
- WS2812B RGB LED Strip: GPIO 15 (Data Pin)
- TTP223 Capacitive Touch Sensor: GPIO 4 (Digital Input)
- OLED Display (SSD1306/SH1106 I2C 128x64): GPIO 21 (SDA), GPIO 22 (SCL)
- Vibration Motor Module: GPIO 13 (PWM/Digital Output)
- DFPlayer Mini MP3 Player: GPIO 16 (RX2), GPIO 17 (TX2) via HardwareSerial2

### 2. CORE ROMANTIC FEATURES & MODULES REQUIRED

1. Non-Blocking Architecture:
   - Must NOT use delay(). Use millis() state machines for touch debounce, vibration pulses, LED animations, and display screen switching.

2. WiFiManager & Config Storage (NVS / Preferences.h):
   - Captive Portal AP "LoveLamp_Setup" to configure Wi-Fi, Pair ID, Role (A or B), and MQTT credentials.
   - Use `Preferences.h` to store all configurations, including a serialized JSON string for "Special Dates / Anniversaries" list.

3. NTP Time Sync & Multiple Anniversaries Reminder Engine:
   - Connect to NTP Server (`pool.ntp.org`) to maintain current Date and Time.
   - Memory Storage: Store an array of special dates via JSON.
   - Trigger Logic: Once a day, check if current DD/MM matches any saved special dates.
   - Alert Action: Display scrolling congratulatory message on OLED, play special celebration track via DFPlayer, trigger Heartbeat Vibration, and play a Rainbow/Pink pulsing effect on LEDs.

4. Heartbeat Vibration Engine (GPIO 13):
   - When `vibrate: true` is received via MQTT, local touch, or Anniversary Alert, trigger a realistic double-pulse vibration sequence (100ms ON -> 100ms OFF -> 100ms ON -> 600ms OFF, repeat 3 times).

5. Audio Player (DFPlayer Mini on Serial2):
   - Track 1: Romantic Chime (touch/messages).
   - Track 2: Lullaby (Mood = SLEEPING).
   - Track 3: Celebration song (Anniversary Alert).

6. OLED Display Manager:
   - Screen Rotation (every 5 seconds): Time/Date, Mood status, Secret Love Note, and Anniversary Alert message.

7. Responsive Web UI (Served from ESP32 WebServer + mDNS `http://<custom_name>.local`):
   - Tab 1 [Controls]: Color Picker, Brightness, Power Toggle.
   - Tab 2 [Moods]: Miss You, Sleeping, Working, Need Hug.
   - Tab 3 [Love Notes]: Text input to send custom messages.
   - Tab 4 [Sounds]: Buttons to trigger chime/lullaby on partner's lamp.
   - Tab 5 [Special Dates]: UI to add/delete multiple special dates (Name, Day, Month) with Fetch API saving to ESP32.
   - Tab 6 [Settings & OTA]: Firmware version, Pair ID settings, "Check Update" button, and "Update Now" button.

8. Dedicated OTA Module (`OTA.h`):
   - Encapsulate all GitHub OTA update logic in a separate modular header file (`OTA.h`).
   - Functions required:
     * `checkGitHubUpdate(String &latestVersion, String &binUrl)`: Fetches `version.json` from GitHub over HTTPS.
     * `performOTAUpdate(const char* binUrl)`: Triggers `http->update()` to download `firmware.bin` and restart upon success.
   - Triggered ONLY when user clicks "Update Now" in Tab 6 of the Web UI.

9. MQTT Synchronization:
   - PubSubClient listening/publishing JSON payload to topic: `lovelamp/{PairID}/from_{Role}`. Synchronize color, brightness, mood, love note, vibration trigger, and sound track.

### 3. EXPECTED DELIVERABLES & FILE STRUCTURE
Provide modular PlatformIO / Arduino C++ code files:
1. `platformio.ini` with all library dependencies (`WiFiManager`, `PubSubClient`, `Adafruit_NeoPixel`, `Adafruit_SSD1306`, `DFRobotDFPlayerMini`, `ArduinoJson`, `HTTPUpdate`).
2. `main.cpp` (clean state-machine implementation).
3. `Config.h` (pinouts, system defaults, current version string, GitHub raw URLs).
4. `OTA.h` (dedicated GitHub OTA check and update execution functions).
5. `WebPage.h` (Minified HTML/CSS/JS in PROGMEM, implementing all 6 tabs and API endpoints).