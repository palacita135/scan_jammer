// === RF Scanner for 915 MHz with Flipper Control & OLED UI ===
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// === OLED Setup ===
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET     -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// === Onboard Buttons ===
#define BUTTON_NEXT 32
#define BUTTON_PAUSE 33
#define BUTTON_RESET 25
#define BTN_JAM_MODE 26

// === UART to 3DR Radio ===
#define RADIO_TX 17
#define RADIO_RX 16

// === Frequencies to Scan in 915 MHz Band (902 - 928 MHz) ===
const uint32_t freqList[] = {
  902000000, 903000000, 904000000, 905000000,
  906000000, 907000000, 908000000, 909000000,
  910000000, 911000000, 912000000, 913000000,
  914000000, 915000000, 916000000, 917000000,
  918000000, 919000000, 920000000, 921000000,
  922000000, 923000000, 924000000, 925000000,
  926000000, 927000000, 928000000
};
uint8_t freqIndex = 0;

bool scanning = true;
bool jamMode = false;
unsigned long lastUpdate = 0;
const unsigned long scanInterval = 800; // Faster scan interval

// === Debounce ===
bool lastNextButton = HIGH;
bool lastPauseButton = HIGH;
bool lastResetButton = HIGH;
bool lastJamButton = HIGH;

void setup() {
  Serial.begin(115200);
  Serial2.begin(57600, SERIAL_8N1, RADIO_RX, RADIO_TX);

  pinMode(BUTTON_NEXT, INPUT_PULLUP);
  pinMode(BUTTON_PAUSE, INPUT_PULLUP);
  pinMode(BUTTON_RESET, INPUT_PULLUP);
  pinMode(BTN_JAM_MODE, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true); // OLED failure halt
  }

  display.clearDisplay();
  configure3DR();
  sendFrequencyToRadio(freqList[freqIndex]);
  drawStatus();
}

void loop() {
  bool nextButton = digitalRead(BUTTON_NEXT);
  bool pauseButton = digitalRead(BUTTON_PAUSE);
  bool resetButton = digitalRead(BUTTON_RESET);
  bool jamButton = digitalRead(BTN_JAM_MODE);

  if (nextButton == LOW && lastNextButton == HIGH) {
    if (!scanning) scanning = true;
    nextFreq();
  }
  if (pauseButton == LOW && lastPauseButton == HIGH) {
    scanning = !scanning;
    drawStatus();
  }
  if (resetButton == LOW && lastResetButton == HIGH) {
    freqIndex = 0;
    scanning = true;
    drawStatus();
  }
  if (jamButton == LOW && lastJamButton == HIGH) {
    jamMode = !jamMode;
    drawStatus();
  }

  lastNextButton = nextButton;
  lastPauseButton = pauseButton;
  lastResetButton = resetButton;
  lastJamButton = jamButton;

  if (scanning && millis() - lastUpdate > scanInterval) {
    lastUpdate = millis();
    nextFreq();
  }
}

void nextFreq() {
  freqIndex = (freqIndex + 1) % (sizeof(freqList) / sizeof(freqList[0]));
  uint32_t freq = freqList[freqIndex];
  sendFrequencyToRadio(freq);
  drawStatus();
}

void sendFrequencyToRadio(uint32_t freq) {
  Serial2.print(jamMode ? "JAM " : "WATCH ");
  Serial2.println(freq);

  unsigned long timeout = millis() + 1000;
  String response = "";
  while (millis() < timeout) {
    while (Serial2.available()) {
      char c = Serial2.read();
      response += c;
    }
    if (response.length() > 0) break;
  }

  if (response.length() == 0 || response.indexOf("OK") == -1) {
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
  display.setCursor(0, 30);
  display.print("FREQ: ");
  display.println(freqList[freqIndex]);
  display.setCursor(0, 50);
  display.print("915 MHz BAND");
  display.display();
}

void drawNoSignal() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
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
