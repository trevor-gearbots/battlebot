//Transmitter
#include <esp_now.h>
#include <WiFi.h>

#define JOY_X 32
#define JOY_Y 33

uint8_t receiverMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};  // you need to put your Receiver (RX) MAC address here!  ask for help

const char ID[] = "CAR01";

typedef struct {
  char id[6];
  int8_t left;
  int8_t right;
  uint8_t seq;
} ControlPacket;

ControlPacket packet;

// timing
unsigned long lastSend = 0;
const int interval = 50;

// diagnostics
volatile uint32_t sendOK = 0;
volatile uint32_t sendFail = 0;
unsigned long lastSendAttemptTime = 0;
int8_t dbg_left = 0;
int8_t dbg_right = 0;
uint8_t dbg_seq = 0;

// sequence
uint8_t seq = 0;

// settings
#define DEADZONE 10
#define SMOOTH_SAMPLES 4
#define USE_EXPO true

// -------- input --------

int readAxisRaw(int pin) {
  long sum = 0;
  for (int i = 0; i < SMOOTH_SAMPLES; i++) {
    sum += analogRead(pin);
  }
  return sum / SMOOTH_SAMPLES;
}

int processAxis(int raw) {
  int v = map(raw, 0, 4095, -127, 127);

  if (abs(v) < DEADZONE) v = 0;

  if (USE_EXPO && v != 0) {
    float f = v / 127.0f;
    f = f * f * (f > 0 ? 1 : -1);
    v = (int)(f * 127);
  }

  // clamp defensively
  if (v > 127) v = 127;
  if (v < -127) v = -127;

  return v;
}

// -------- ESP-NOW send callback --------

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    sendOK++;
  } else {
    sendFail++;
  }
}

// -------- setup --------

void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

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

// -------- loop --------

void loop() {
  unsigned long now = millis();

  // --- send control packets ---
  if (now - lastSend >= interval) {
    lastSend = now;
    lastSendAttemptTime = now;

    int rawX = readAxisRaw(JOY_X);
    int rawY = readAxisRaw(JOY_Y);

    int x = processAxis(rawX);
    int y = processAxis(rawY);

    // differential mixing
    int left  = y + x;
    int right = y - x;

    left  = constrain(left,  -127, 127);
    right = constrain(right, -127, 127);

    // fill packet
    strncpy(packet.id, ID, sizeof(packet.id));
    packet.id[5] = '\0';

    packet.left  = left;
    packet.right = right;
    packet.seq   = seq++;
    dbg_left  = left;
    dbg_right = right;
    dbg_seq   = seq;

    esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));
  }

  // --- link warning (no successful sends) ---
  static unsigned long lastWarnPrint = 0;

  if (now - lastSendAttemptTime >= 5000) {
    if (now - lastWarnPrint >= 5000) {
      Serial.println("WARN: no send attempts (5s)");
      lastWarnPrint = now;
    }
  } else {
    lastWarnPrint = 0;
  }

  // --- 1 Hz status ---
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
