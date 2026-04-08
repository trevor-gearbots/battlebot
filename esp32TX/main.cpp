//Transmitter

#include <esp_now.h>
#include <WiFi.h>

#define JOY_X 32
#define JOY_Y 33

// Receiver MAC (CHANGE THIS)
uint8_t receiverMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};

// 5-character ID (must match receiver)
const char ID[] = "CAR01";

typedef struct {
  char id[6];     // 5 chars + null
  int8_t left;
  int8_t right;
} ControlPacket;

ControlPacket packet;

unsigned long lastSend = 0;
const int interval = 50;

// ----- SETTINGS -----
#define DEADZONE 10
#define SMOOTH_SAMPLES 4
#define USE_EXPO true

// ----- FUNCTIONS -----

// Simple averaging filter
int readAxisRaw(int pin) {
  long sum = 0;
  for (int i = 0; i < SMOOTH_SAMPLES; i++) {
    sum += analogRead(pin);
  }
  return sum / SMOOTH_SAMPLES;
}

// Map + deadband + optional expo
int processAxis(int raw) {
  int v = map(raw, 0, 4095, -127, 127);

  // Deadzone
  if (abs(v) < DEADZONE) v = 0;

  // Exponential response (better low-speed control)
  if (USE_EXPO && v != 0) {
    float f = v / 127.0f;
    f = f * f * (f > 0 ? 1 : -1);
    v = (int)(f * 127);
  }

  return v;
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("TX MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }
}

void loop() {
  if (millis() - lastSend >= interval) {
    lastSend = millis();

    int rawX = readAxisRaw(JOY_X);
    int rawY = readAxisRaw(JOY_Y);

    int x = processAxis(rawX);
    int y = processAxis(rawY);

    // Differential drive mix
    int left  = y + x;
    int right = y - x;

    left  = constrain(left,  -127, 127);
    right = constrain(right, -127, 127);

    // Fill packet
    strncpy(packet.id, ID, sizeof(packet.id));
    packet.id[5] = '\0';

    packet.left  = left;
    packet.right = right;

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));

    // Optional debug
    if (result == ESP_OK) {
      Serial.printf("L:%d R:%d\n", left, right);
    } else {
      Serial.println("Send error");
    }
  }
}
