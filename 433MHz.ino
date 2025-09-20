#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_task_wdt.h> // For ESP32 reset

// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === GPIO Definitions ===
#define BUTTON_NEXT   32   // Next frequency
#define BUTTON_PAUSE  33   // Pause/Resume or hold to toggle jam
#define BUTTON_RESET  25   // Reset frequency index or unlock
#define BTN_RESET_ESP 26   // Reset ESP32 (formerly BTN_JAM_MODE)

// === UART Setup ===
#define RADIO_RX 16      // Connect to 3DR Radio TX
#define RADIO_TX 17      // Connect to 3DR Radio RX

// === 433MHz Drone Frequencies ===
const uint32_t droneFreqs[] = {
  433075000, 433175000, 433275000, 433375000, 433475000,
  433575000, 433675000, 433775000, 433875000, 433920000,
  434075000, 434175000, 434275000, 434375000, 434475000,
  434575000, 434675000, 434775000, 434875000
};

// === State Variables ===
bool scanning = true;
bool jamMode = false;
bool frequencyLocked = false;
uint32_t lockedFrequency = 0;
uint8_t freqIndex = 0;
unsigned long lastUpdate = 0;
const unsigned long scanInterval = 500; // 0.5s between scans

// === Debounce & Long Press ===
bool lastPauseButton = HIGH;
unsigned long pausePressStart = 0;
bool pauseHeld = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RADIO_RX, RADIO_TX); // UART for 3DR

  // Initialize buttons
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PAUSE, INPUT_PULLUP);
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  pinMode(BTN_RESET_ESP, INPUT_PULLUP);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED failed! Check wiring."));
    while (1);
  }

  // Test OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println("Initializing...");
  display.println("3DR 433MHz Scanner");
  display.display();
  delay(1000);

  // Test 3DR
  display.clearDisplay();
  display.println("Testing 3DR...");
  display.display();
  Serial.println("Testing 3DR Radio...");

  if (!test3DR()) {
    display.clearDisplay();
    display.println("3DR FAILED!");
    display.println("Check wiring/power");
    display.display();
    while (1); // Halt if 3DR fails
  }

  display.clearDisplay();
  display.println("3DR: OK");
  display.println("Starting scan...");
  display.display();

  configure3DR();
  sendFrequency(droneFreqs[freqIndex], false); // Start scanning
  drawStatus();
  Serial.println("Setup complete. Scanning 433MHz...");
}

void loop() {
  // === Reset ESP32 Button ===
  if (digitalRead(BTN_RESET_ESP) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BTN_RESET_ESP) == LOW) {
      display.clearDisplay();
      display.println("RESETTING ESP32...");
      display.display();
      Serial.println("Resetting ESP32...");
      delay(1000);
      ESP.restart(); // Reset ESP32
    }
  }

  // === Pause Button Logic ===
  bool pauseButton = digitalRead(BUTTON_PAUSE);
  if (pauseButton == LOW && lastPauseButton == HIGH) {
    pausePressStart = millis();
    pauseHeld = false;
  }
  if (pauseButton == LOW && !pauseHeld && millis() - pausePressStart >= 2000) {
    // Long press: Toggle jam mode
    jamMode = !jamMode;
    pauseHeld = true;
    if (jamMode && frequencyLocked) {
      sendFrequency(lockedFrequency, true); // Jam locked frequency
      Serial.println("Jamming locked frequency!");
    } else if (frequencyLocked) {
      sendFrequency(lockedFrequency, false); // Stop jamming, stay locked
      Serial.println("Stopped jamming, staying locked.");
    } else {
      sendFrequency(droneFreqs[freqIndex], false); // Resume scanning
      Serial.println("Resumed scanning.");
    }
    drawStatus();
  }
  if (pauseButton == HIGH && lastPauseButton == LOW && !pauseHeld) {
    // Short press: Toggle pause/resume
    scanning = !scanning;
    drawStatus();
    Serial.print("Scanning ");
    Serial.println(scanning ? "resumed" : "paused");
  }
  lastPauseButton = pauseButton;

  // === Next Button ===
  if (digitalRead(BUTTON_NEXT) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_NEXT) == LOW) {
      nextFreq();
      while (digitalRead(BUTTON_NEXT) == LOW); // Wait for release
    }
  }

  // === Reset Button (Unlock) ===
  if (digitalRead(BUTTON_RESET) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_RESET) == LOW) {
      frequencyLocked = false;
      jamMode = false;
      scanning = true;
      sendFrequency(droneFreqs[freqIndex], false);
      drawStatus();
      Serial.println("Reset. Scanning resumed.");
      while (digitalRead(BUTTON_RESET) == LOW); // Wait for release
    }
  }

  // === Auto-Scan ===
  if (scanning && !frequencyLocked && millis() - lastUpdate > scanInterval) {
    lastUpdate = millis();
    nextFreq();
  }
}

bool test3DR() {
  Serial.println("Testing 3DR...");

  // Try entering AT mode
  Serial2.print("+++");
  delay(1000); // Wait for AT mode

  // Request info
  Serial2.println("ATI");
  delay(500);

  String response = "";
  while (Serial2.available()) {
    response += (char)Serial2.read();
  }

  Serial.print("3DR Response: ");
  Serial.println(response);

  if (response.indexOf("OK") != -1 || response.indexOf("RFD900") != -1) {
    Serial.println("3DR Test: PASSED");
    return true;
  } else {
    Serial.println("3DR Test: FAILED");

    // Try alternative baud rate
    Serial.println("Trying 115200 baud...");
    Serial2.end();
    delay(100);
    Serial2.begin(115200, SERIAL_8N1, RADIO_RX, RADIO_TX);
    delay(100);

    Serial2.print("+++");
    delay(1000);
    Serial2.println("ATI");
    delay(500);

    response = "";
    while (Serial2.available()) {
      response += (char)Serial2.read();
    }

    Serial.print("3DR Response (115200): ");
    Serial.println(response);

    if (response.indexOf("OK") != -1 || response.indexOf("RFD900") != -1) {
      Serial.println("3DR Test: PASSED at 115200 baud");
      Serial2.end();
      delay(100);
      Serial2.begin(115200, SERIAL_8N1, RADIO_RX, RADIO_TX);
      return true;
    } else {
      Serial.println("3DR Test: FAILED at all baud rates");
      return false;
    }
  }
}

void nextFreq() {
  if (frequencyLocked) return; // Stay locked

  freqIndex = (freqIndex + 1) % (sizeof(droneFreqs) / sizeof(droneFreqs[0]));
  sendFrequency(droneFreqs[freqIndex], false);
  checkForSignal(droneFreqs[freqIndex]);
  drawStatus();
}

void checkForSignal(uint32_t freq) {
  Serial2.print("WATCH ");
  Serial2.println(freq);
  delay(100);

  String response = "";
  unsigned long timeout = millis() + 500;
  while (millis() < timeout) {
    while (Serial2.available()) {
      response += (char)Serial2.read();
    }
  }

  Serial.print("Response for ");
  Serial.print(freq/1e6, 3);
  Serial.print(" MHz: ");
  Serial.println(response);

  // Check for signal detection (ACK or RSSI)
  if (response.indexOf("ACK") != -1 || response.indexOf("RSSI") != -1) {
    frequencyLocked = true;
    lockedFrequency = freq;
    scanning = false;
    jamMode = true;
    sendFrequency(lockedFrequency, true); // Start jamming
    Serial.print("SIGNAL DETECTED! Locked onto: ");
    Serial.print(freq/1e6, 3);
    Serial.println(" MHz");
  }
}

void sendFrequency(uint32_t freq, bool jam) {
  Serial.print("Setting: ");
  Serial.print(jam ? "JAM " : "WATCH ");
  Serial.print(freq/1e6, 3);
  Serial.println(" MHz");

  Serial2.print(jam ? "JAM " : "WATCH ");
  Serial2.println(freq);
}

void drawStatus() {
  display.clearDisplay();
  display.setCursor(0, 0);
  if (frequencyLocked) {
    display.println("LOCKED ON SIGNAL!");
  } else {
    display.print(scanning ? "SCANNING..." : "PAUSED");
  }

  display.setCursor(0, 10);
  display.print("MODE: ");
  display.println(jamMode ? "JAM" : "SCAN");

  display.setCursor(0, 20);
  display.print("FREQ: ");
  display.print(frequencyLocked ? lockedFrequency/1e6 : droneFreqs[freqIndex]/1e6, 3);
  display.println(" MHz");

  display.setCursor(0, 30);
  if (frequencyLocked) {
    display.print(jamMode ? "JAMMING ACTIVE!" : "LOCKED (no jam)");
  } else {
    display.println("Press NEXT to scan");
  }

  display.setCursor(0, 40);
  display.println("Hold PAUSE to jam");

  display.setCursor(0, 50);
  display.print("Press BTN to reset");

  display.display();
}

void configure3DR() {
  delay(1000);
  Serial2.print("+++"); // Enter AT mode
  delay(1000);
  Serial2.println("ATI"); // Request info
  delay(500);
  Serial2.println("ATBD=57600"); // Set baud rate
  delay(500);
  Serial2.println("ATWR"); // Save settings
  delay(500);
  Serial2.println("ATCN"); // Exit AT mode
  delay(500);
  Serial.println("3DR Radio configured.");
}
