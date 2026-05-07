#include <WiFi.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

// ==== Display ====
TFT_eSPI tft = TFT_eSPI();
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define BORDER 5

#define FREQ_MIN 2402
#define FREQ_MAX 2482

#define GUIDE_COLOR 0x4208
#define GUIDE_COLOR2 0xD69A


// ==== Button (adjust if needed for your CYD) ====
#define BTN_PIN 0

// backlight
#define TFT_BL 21      // should be 21
#define TFT_BACKLIGHT_ON HIGH

#define RLED_PIN 4
#define GLED_PIN 16
#define BLED_PIN 17

// ==== Scan timing ====
unsigned long lastScan = 0;
const unsigned long SCAN_INTERVAL = 60000; // 60s

unsigned long lastStatus = 0;
const unsigned long STATUS_INTERVAL = 5000;

bool scanRunning = false;

// ==== Helpers ====
int freqFromChannel(int ch) {
  return 2407 + (ch * 5);
}

int xForFreq(int freq) {
  return map(freq, FREQ_MIN, FREQ_MAX, BORDER, tft.width() - BORDER);
}

int xForChannel(int ch) {
  return xForFreq(freqFromChannel(ch));
}

int heightForRSSI(int rssi) {
  return map(
    constrain(rssi, -100, -30),
    -100,
    -30,
    0,
    tft.height() - (BORDER * 2)
  );
}

uint16_t colorForSSID(const String& s) {
  uint32_t h = 5381;
  for (size_t i = 0; i < s.length(); i++) {
    h = ((h << 5) + h) + s[i];
  }
  return (uint16_t)(h & 0xFFFF);
}

int widthForMHz(int mhz) {
  int pxPerCh = tft.width() / 13; // ~40 MHz bucket
  return (mhz == 40) ? pxPerCh * 2 : pxPerCh;
}


// ==== Drawing ====
void drawGuides() {

  int chs[] = {1, 6, 11};

  for (int ch = 1; ch <=13; ch+=1) {
    int x = xForFreq(freqFromChannel(ch));
    tft.drawLine(x, BORDER, x, tft.height() - BORDER, GUIDE_COLOR);
  }


  for (int i = 0; i < 3; i++) {

    int ch = chs[i];
    int freq = freqFromChannel(ch);
    int x = xForFreq(freq);

    tft.drawLine(x, BORDER, x, tft.height() - BORDER, GUIDE_COLOR2);

    // label
    String label = "Ch" + String(ch);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);

    tft.drawString(label, x, BORDER, 1);
    
  }
}

// gaussian curve
void drawAP(const String& ssid, int ch, int rssi, int mhz = 40) {

  int freq = freqFromChannel(ch);

  float centerX = xForFreq(freq);

  float peakHeight = heightForRSSI(rssi);

  // Gaussian width
  // 20 MHz ~= narrower
  // 40 MHz ~= wider
  float sigma =
    (mhz == 40)
      ? 18.0
      : 10.0;

  uint16_t col = colorForSSID(ssid);

  int prevX = -1;
  int prevY = -1;

  for (int x = BORDER; x < tft.width() - BORDER; x++) {

    float dx = (x - centerX);

    float yNorm = exp(
      -(dx * dx) / (2.0 * sigma * sigma)
    );

    int y =
      (tft.height() - BORDER) -
      (peakHeight * yNorm);

    if (y < BORDER) {
      y = BORDER;
    }

    // draw continuous curve
    if (prevX >= 0) {
      tft.drawLine(prevX, prevY, x, y, col);
    }

    prevX = x;
    prevY = y;
  }

  // SSID label near peak
  tft.setTextColor(col, TFT_BLACK);

  int textX = centerX + 4;
  int textY =
    (tft.height() - BORDER) -
    peakHeight -
    8;

  if (textX > tft.width() - 60) {
    textX = centerX - 60;
  }

  if (textY < BORDER) {
    textY = BORDER;
  }

  tft.drawString(ssid, textX, textY, 1);
}


void drawResults(int n) {
  tft.fillScreen(TFT_BLACK);
  drawGuides();

  for (int i = 0; i < n; i++) {

    String ssid = WiFi.SSID(i);

    if (ssid.length() == 0) {
      ssid = "hidden";
    }
    
    // ESP32 scan doesn't reliably expose 40 MHz; use 20 MHz by default
    drawAP(WiFi.SSID(i), WiFi.channel(i), WiFi.RSSI(i), 40);
  }
}

// ==== WiFi scan control (async) ====
void startScan() {
  if (scanRunning) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true); // async, show_hidden
  scanRunning = true;
}

// serial monitor
void printStatus() {
  int n = WiFi.scanComplete(); // -2 idle, -1 running, >=0 results
  int maxRSSI = -100;
  if (n > 0) {
    for (int i = 0; i < n; i++) {
      int r = WiFi.RSSI(i);
      if (r > maxRSSI) maxRSSI = r;
    }
  }
  // time_ms,networks,max_rssi,scan_running
  Serial.print(millis());
  Serial.print(',');
  Serial.print(n >= 0 ? n : 0);
  Serial.print(',');
  Serial.print(maxRSSI);
  Serial.print(',');
  Serial.println(scanRunning ? 1 : 0);
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  pinMode(RLED_PIN, OUTPUT);
  digitalWrite(RLED_PIN, LOW); // on

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  drawGuides();
  Serial.println(tft.width());
  Serial.println(tft.height());

  lastScan = millis() - SCAN_INTERVAL; // trigger immediate first scan

  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_GREEN);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);

  tft.fillScreen(TFT_BLACK);
  tft.drawString("DISPLAY OK", 10, 10, 1);
  delay(500);

  pinMode(GLED_PIN, OUTPUT);
  digitalWrite(RLED_PIN, HIGH); // off
  digitalWrite(GLED_PIN, LOW); // on

}

// ==== Loop ====
void loop() {
  // Button (active low)
  static bool lastBtn = HIGH;
  bool btn = digitalRead(BTN_PIN);
  if (lastBtn == HIGH && btn == LOW) {
    startScan();
    lastScan = millis();
  }
  lastBtn = btn;

  // Periodic scan
  if (millis() - lastScan > SCAN_INTERVAL) {
    startScan();
    lastScan = millis();
  }

  // Check async completion
  int n = WiFi.scanComplete();
  if (scanRunning && n >= 0) {
    drawResults(n);
    scanRunning = false;
    WiFi.scanDelete();
  }

  if (millis() - lastStatus > STATUS_INTERVAL) {
    printStatus();
    lastStatus = millis();
  }

  delay(10);
}
