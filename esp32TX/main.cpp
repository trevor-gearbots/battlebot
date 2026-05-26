//Transmitter
#include <esp_now.h>
#include <WiFi.h>

// ---------------- Joystick ----------------

#define JOY_X 32
#define JOY_Y 33

// ---------------- Receiver MAC ----------------

uint8_t receiverMAC[] = {
  0x00, 0x4B, 0x12, 0x3C, 0x4A, 0x24
};

// ---------------- Packet ----------------

typedef struct {
  int8_t left;
  int8_t right;
  uint8_t seq;
} ControlPacket;

ControlPacket packet;

// ---------------- Timing ----------------

unsigned long lastSend = 0;
const int interval = 20;

// ---------------- Diagnostics ----------------

volatile uint32_t sendOK = 0;
volatile uint32_t sendFail = 0;

unsigned long lastSendAttemptTime = 0;

int8_t dbg_left = 0;
int8_t dbg_right = 0;
uint8_t dbg_seq = 0;

// ---------------- Sequence ----------------

uint8_t seq = 0;

// ---------------- Settings ----------------

#define DEADZONE 10
#define SMOOTH_SAMPLES 4
#define USE_EXPO true

#define JOY_CENTER_X 2048
#define JOY_CENTER_Y 2048

// ---------------- Input ----------------

int readAxisRaw(int pin) {
  long sum = 0;

  for (int i = 0; i < SMOOTH_SAMPLES; i++) {
    sum += analogRead(pin);
  }

  return sum / SMOOTH_SAMPLES;
}

int processAxis(int raw, int center) {
  int v = raw - center;

  // normalize around center
  if (v > 0) {
    v = map(v, 0, 2047, 0, 127);
  }
  else {
    v = map(v, -2048, 0, -127, 0);
  }

  if (abs(v) < DEADZONE) {
    v = 0;
  }

  if (USE_EXPO && v != 0) {
    float f = v / 127.0f;
    f = f * f * (f > 0 ? 1.0f : -1.0f);
    v = (int)(f * 127);
  }

  v = constrain(v, -127, 127);

  return v;
}

// ---------------- ESP-NOW callback ----------------

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    sendOK++;
  }
  else {
    sendFail++;
  }
}

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  Serial.println("TX Boot");

  WiFi.mode(WIFI_STA);

  Serial.print("TX MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    return;
  }

  Serial.println("ESP-NOW init OK");

  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peer = {};

  memcpy(peer.peer_addr, receiverMAC, 6);

  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Add peer FAILED");
    return;
  }

  Serial.println("Peer added");
}

// ---------------- Loop ----------------

void loop() {
  unsigned long now = millis();

  // ----- send packets -----

  if (now - lastSend >= interval) {
    lastSend = now;
    lastSendAttemptTime = now;

    int rawX = readAxisRaw(JOY_X);
    int rawY = readAxisRaw(JOY_Y);

    int x = processAxis(rawX, JOY_CENTER_X);
    int y = processAxis(rawY, JOY_CENTER_Y);

    // differential mixing
    int left = y + x;
    int right = y - x;

    left = constrain(left, -127, 127);
    right = constrain(right, -127, 127);

    packet.left = left;
    packet.right = right;
    packet.seq = seq++;

    dbg_left = left;
    dbg_right = right;
    dbg_seq = packet.seq;

    esp_now_send(
      receiverMAC,
      (uint8_t*)&packet,
      sizeof(packet)
    );
  }

  // ----- warnings -----

  static unsigned long lastWarnPrint = 0;

  if (now - lastSendAttemptTime >= 5000) {
    if (now - lastWarnPrint >= 5000) {
      Serial.println("WARN: no send attempts (5s)");
      lastWarnPrint = now;
    }
  }
  else {
    lastWarnPrint = 0;
  }

  // ----- status -----

  static unsigned long lastPrint = 0;

  if (now - lastPrint >= 1000) {
    Serial.printf(
      "TX | L:%d R:%d Seq:%u OK:%u FAIL:%u Age:%lu ms\n",
      dbg_left,
      dbg_right,
      dbg_seq,
      sendOK,
      sendFail,
      now - lastSendAttemptTime
    );

    sendOK = 0;
    sendFail = 0;

    lastPrint = now;
  }
}
