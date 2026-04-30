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
  char id[6];
  int8_t left;
  int8_t right;
} ControlPacket;

ControlPacket packet;

unsigned long lastSend = 0;
const int interval = 50;

// Debug
unsigned long lastDebugPrint = 0;
int sendFailCount = 0;

// ----- SETTINGS -----
#define DEADZONE 10
#define SMOOTH_SAMPLES 4
#define USE_EXPO true

// ---------- Helpers ----------

void printMAC(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  Serial.print(buf);
}

// ---------- Input processing ----------

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

  return v;
}

// ---------- ESP-NOW send callback ----------

void onSend(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Send status to ");
  printMAC(mac_addr);
  Serial.print(" : ");

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("OK");
    sendFailCount = 0;
  } else {
    Serial.println("FAIL");
    sendFailCount++;
  }
}

// ---------- Setup ----------

void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial) {
    if (millis() - start > 5000) break;
    delay(10);
  }

  Serial.println("\nBooting transmitter...");

  WiFi.mode(WIFI_STA);

  Serial.print("STA MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Target peer: ");
  printMAC(receiverMAC);
  Serial.println();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed");
    return;
  }

  Serial.println("ESP-NOW initialized");

  esp_now_register_send_cb(onSend);
  Serial.println("Send callback registered");

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, receiverMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("ERROR: Failed to add peer");
    return;
  }

  Serial.println("Peer added successfully");
}

// ---------- Loop ----------

void loop() {
  if (millis() - lastSend >= interval) {
    lastSend = millis();

    int rawX = readAxisRaw(JOY_X);
    int rawY = readAxisRaw(JOY_Y);

    int x = processAxis(rawX);
    int y = processAxis(rawY);

    int left  = constrain(y + x,  -127, 127);
    int right = constrain(y - x, -127, 127);

    strncpy(packet.id, ID, sizeof(packet.id));
    packet.id[5] = '\0';

    packet.left  = left;
    packet.right = right;

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t*)&packet, sizeof(packet));

    if (result != ESP_OK) {
      Serial.print("Immediate send error: ");
      Serial.println(result);
      sendFailCount++;
    }

    // Rate-limited debug (250 ms)
    if (millis() - lastDebugPrint > 250) {
      lastDebugPrint = millis();

      Serial.print("RAW X:");
      Serial.print(rawX);
      Serial.print(" Y:");
      Serial.print(rawY);

      Serial.print(" | OUT L:");
      Serial.print(left);
      Serial.print(" R:");
      Serial.print(right);

      Serial.print(" | FailCount:");
      Serial.println(sendFailCount);
    }
  }
}
