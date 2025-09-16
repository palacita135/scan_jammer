#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === GPIO Definitions ===
#define BUTTON_NEXT  32   // Next frequency
#define BUTTON_PAUSE 33   // Pause/Resume or hold to switch band
#define BUTTON_RESET 25   // Reset frequency index
#define BTN_JAM_MODE 26  // Toggle jam mode

// === UART Setup ===
#define RADIO_TX 17      // UART TX to 3DR Radio
#define RADIO_RX 16      // UART RX from 3DR Radio

// === Frequency Lists ===
const uint32_t freqList433[] = {
  433050000, 433100000, 433150000, 433200000, 433250000, 433300000,
  433350000, 433400000, 433450000, 433500000, 433550000, 433600000,
  433650000, 433700000, 433750000, 433800000, 433850000, 433900000,
  433950000, 434000000, 434050000
};
const uint32_t freqList915[] = {
  900000000, 902000000, 904000000, 906000000, 908000000,
  910000000, 912000000, 914000000, 916000000, 918000000,
  920000000, 922000000, 924000000, 926000000, 928000000
};

// === State Variables ===
bool use433Band = true;
bool scanning = true;
bool jamMode = false;
uint8_t freqIndex = 0;
unsigned long lastUpdate = 0;
const unsigned long scanInterval = 1000; // 1 second between scans

// === Debounce & Long Press ===
bool lastPauseButton = HIGH;
unsigned long pausePressStart = 0;
bool pauseHeld = false;

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RADIO_RX, RADIO_TX);

  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PAUSE, INPUT_PULLUP);
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  pinMode(BTN_JAM_MODE, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED not found!"));
    while (true);
  }

  display.clearDisplay();
  configure3DR();
  sendFrequencyToRadio(getCurrentFreq());
  drawStatus();
}

void loop() {
  // === Pause Button Logic ===
  bool pauseButton = digitalRead(BUTTON_PAUSE);
  if (pauseButton == LOW && lastPauseButton == HIGH) {
    pausePressStart = millis();
    pauseHeld = false;
  }
  if (pauseButton == LOW && !pauseHeld && millis() - pausePressStart >= 2000) {
    // Long press: Switch band
    use433Band = !use433Band;
    freqIndex = 0;
    scanning = true;
    pauseHeld = true;
    sendFrequencyToRadio(getCurrentFreq());
    drawStatus();
  }
  if (pauseButton == HIGH && lastPauseButton == LOW) {
    if (!pauseHeld) {
      // Short press: Toggle pause/resume
      scanning = !scanning;
      drawStatus();
    }
  }
  lastPauseButton = pauseButton;

  // === Next Button ===
  if (digitalRead(BUTTON_NEXT) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_NEXT) == LOW) {
      scanning = true;
      nextFreq();
    }
  }

  // === Reset Button ===
  if (digitalRead(BUTTON_RESET) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_RESET) == LOW) {
      freqIndex = 0;
      scanning = true;
      drawStatus();
    }
  }

  // === Jam Mode Button ===
  if (digitalRead(BTN_JAM_MODE) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BTN_JAM_MODE) == LOW) {
      jamMode = !jamMode;
      drawStatus();
    }
  }

  // === Auto-Scan ===
  if (scanning && millis() - lastUpdate > scanInterval) {
    lastUpdate = millis();
    nextFreq();
  }
}

void nextFreq() {
  freqIndex = (freqIndex + 1) % getFreqListSize();
  sendFrequencyToRadio(getCurrentFreq());
  drawStatus();
}

uint32_t getCurrentFreq() {
  return use433Band ? freqList433[freqIndex] : freqList915[freqIndex];
}

size_t getFreqListSize() {
  return use433Band ? sizeof(freqList433) / sizeof(freqList433[0])
                    : sizeof(freqList915) / sizeof(freqList915[0]);
}

void sendFrequencyToRadio(uint32_t freq) {
  Serial2.print(jamMode ? "JAM " : "WATCH ");
  Serial2.println(freq);
  unsigned long timeout = millis() + 1000;
  String response = "";
  while (millis() < timeout) {
    while (Serial2.available()) {
      response += (char)Serial2.read();
    }
    if (response.length()) break;
  }
  if (response.indexOf("OK") == -1) {
    scanning = false;
    drawNoSignal();
  }
}

void drawStatus() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(scanning ? "SCANNING..." : "PAUSED");
  display.setCursor(0, 10);
  display.print("MODE: ");
  display.print(jamMode ? "JAM" : "WATCH");
  display.setCursor(0, 20);
  display.print("BAND: ");
  display.print(use433Band ? "433 MHz" : "915 MHz");
  display.setCursor(0, 40);
  display.print("FREQ: ");
  display.println(getCurrentFreq());
  display.setCursor(0, 55);
  display.print("Hold PAUSE to switch BAND");
  display.display();
}

void drawNoSignal() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("NO SIGNAL FROM 3DR");
  display.setCursor(0, 20);
  display.println("Check wiring/power");
  display.setCursor(0, 40);
  display.println("Paused scanning.");
  display.display();
}

void configure3DR() {
  delay(1000);
  Serial2.print("+++\r\n");
  delay(1000);
  Serial2.println("ATI");
  delay(500);
  Serial2.println("ATBD=57600");
  delay(500);
  Serial2.println("ATMY=1");
  delay(500);
  Serial2.println("ATDL=2");
  delay(500);
  Serial2.println("ATWR");
  delay(500);
  Serial2.println("ATCN");
  delay(500);
}
