#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === GPIO Definitions ===
#define BUTTON_NEXT   32   // Next frequency
#define BUTTON_PAUSE  33   // Pause/Resume or hold to switch band
#define BUTTON_RESET  25   // Reset frequency index or unlock
#define BTN_JAM_MODE  26   // Toggle jam mode

// === UART Setup ===
#define RADIO_RX 16      // Connect to RFD900x TX
#define RADIO_TX 17      // Connect to RFD900x RX

// === 900MHz RFD900x Frequencies ===
const uint32_t rfdFreqs[] = {
  902000000, 902200000, 902400000, 902600000, 902800000,
  903000000, 903200000, 903400000, 903600000, 903800000,
  904000000, 904200000, 904400000, 904600000, 904800000,
  905000000, 905200000, 905400000, 905600000, 905800000,
  906000000, 906200000, 906400000, 906600000, 906800000,
  907000000, 907200000, 907400000, 907600000, 907800000
};

// === State Variables ===
bool use900Band = true;
bool scanning = true;
bool jamMode = false;
bool frequencyLocked = false;
uint32_t lockedFrequency = 0;
uint8_t freqIndex = 0;
unsigned long lastUpdate = 0;
const unsigned long scanInterval = 500; // 0.5s between scans

// === Debounce Variables ===
bool lastPauseButton = HIGH;
unsigned long pausePressStart = 0;
bool pauseHeld = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RADIO_RX, RADIO_TX); // UART for RFD900x

  // Initialize buttons
  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PAUSE, INPUT_PULLUP);
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  pinMode(BTN_JAM_MODE, INPUT_PULLUP);

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
  display.println("RFD900x 900MHz");
  display.println("Scanner/Jammer");
  display.display();
  delay(1000);

  // Test RFD900x
  display.clearDisplay();
  display.println("Testing RFD900x...");
  display.display();
  Serial.println("Testing RFD900x...");

  if (!testRFD900x()) {
    display.clearDisplay();
    display.println("RFD900x FAILED!");
    display.println("Check wiring/power");
    display.display();
    while (1); // Halt if RFD900x fails
  }

  display.clearDisplay();
  display.println("RFD900x: OK");
  display.println("Starting scan...");
  display.display();

  configureRFD900x();
  sendFrequency(rfdFreqs[freqIndex], false); // Start scanning
  drawStatus();
  Serial.println("Setup complete. Scanning 900MHz...");
}

void loop() {
  // === Jam Mode Button ===
  if (digitalRead(BTN_JAM_MODE) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BTN_JAM_MODE) == LOW) {
      jamMode = !jamMode;
      if (jamMode && frequencyLocked) {
        sendFrequency(lockedFrequency, true); // Start jamming
        Serial.println("Jamming locked frequency!");
      } else if (frequencyLocked) {
        sendFrequency(lockedFrequency, false); // Stop jamming, stay locked
        Serial.println("Stopped jamming, staying locked.");
      } else {
        sendFrequency(rfdFreqs[freqIndex], false); // Resume scanning
        Serial.println("Resumed scanning.");
      }
      drawStatus();
      while (digitalRead(BTN_JAM_MODE) == LOW); // Wait for release
    }
  }

  // === Pause Button Logic ===
  bool pauseButton = digitalRead(BUTTON_PAUSE);
  if (pauseButton == LOW && lastPauseButton == HIGH) {
    pausePressStart = millis();
    pauseHeld = false;
  }
  if (pauseButton == LOW && !pauseHeld && millis() - pausePressStart >= 2000) {
    // Long press: Switch band (not used for 900MHz only)
    pauseHeld = true;
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
      sendFrequency(rfdFreqs[freqIndex], false);
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

bool testRFD900x() {
  Serial.println("Testing RFD900x...");

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

  Serial.print("RFD900x Response: ");
  Serial.println(response);

  if (response.indexOf("OK") != -1 || response.indexOf("RFD900") != -1) {
    Serial.println("RFD900x Test: PASSED");
    return true;
  } else {
    Serial.println("RFD900x Test: FAILED");

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

    Serial.print("RFD900x Response (115200): ");
    Serial.println(response);

    if (response.indexOf("OK") != -1 || response.indexOf("RFD900") != -1) {
      Serial.println("RFD900x Test: PASSED at 115200 baud");
      Serial2.end();
      delay(100);
      Serial2.begin(115200, SERIAL_8N1, RADIO_RX, RADIO_TX);
      return true;
    } else {
      Serial.println("RFD900x Test: FAILED at all baud rates");
      return false;
    }
  }
}

void nextFreq() {
  if (frequencyLocked) return; // Stay locked

  freqIndex = (freqIndex + 1) % (sizeof(rfdFreqs) / sizeof(rfdFreqs[0]));
  sendFrequency(rfdFreqs[freqIndex], false);
  checkForSignal(rfdFreqs[freqIndex]);
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
  display.print(frequencyLocked ? lockedFrequency/1e6 : rfdFreqs[freqIndex]/1e6, 3);
  display.println(" MHz");

  display.setCursor(0, 30);
  if (frequencyLocked) {
    display.print(jamMode ? "JAMMING ACTIVE!" : "LOCKED (no jam)");
  } else {
    display.println("Press NEXT to scan");
  }

  display.setCursor(0, 40);
  display.println("Hold PAUSE for band");

  display.setCursor(0, 50);
  display.print("JAM: ");
  display.println(jamMode ? "ON" : "OFF");

  display.display();
}

void configureRFD900x() {
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
  Serial.println("RFD900x configured.");
}
