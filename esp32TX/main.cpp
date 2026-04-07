#include <esp_now.h>
#include <WiFi.h>

#define JOY_X 32
#define JOY_Y 33

uint8_t receiverMAC[] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC}; // CHANGE

typedef struct {
  int8_t left;
  int8_t right;
} ControlPacket;

ControlPacket packet;

unsigned long lastSend = 0;
const int interval = 50;

int readAxis(int pin) {
  int v = analogRead(pin);          // 0–4095
  v = map(v, 0, 4095, -127, 127);

  // Deadzone
  if (abs(v) < 10) v = 0;

  return v;
}

void setup() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) return;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  esp_now_add_peer(&peer);
}

void loop() {
  if (millis() - lastSend >= interval) {
    lastSend = millis();

    int x = readAxis(JOY_X);
    int y = readAxis(JOY_Y);

    int left  = y + x;
    int right = y - x;

    left  = constrain(left,  -127, 127);
    right = constrain(right, -127, 127);

    packet.left  = left;
    packet.right = right;

    esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));
  }
}
