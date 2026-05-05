//Receiver with one motor driver

#include <esp_now.h>
#include <WiFi.h>

// Motor pins
#define L_A 32
#define L_B 33
#define R_A 25
#define R_B 26

// PWM settings
#define PWM_FREQ 20000
#define PWM_RES 8

// PWM channels
#define L_A_CH 0
#define L_B_CH 1
#define R_A_CH 2
#define R_B_CH 3

// Expected ID
const char expectedID[] = "CAR01";

typedef struct {
  char id[6];
  int8_t left;
  int8_t right;
  uint8_t seq;   // add on TX side as well
} ControlPacket;

volatile ControlPacket current = {"NONE", 0, 0, 0};
volatile uint32_t packetCount = 0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

unsigned long lastPacketTime = 0;
const int timeout = 200;
unsigned long lastNoPacketPrint = 0;

uint8_t lastSeq = 0;
bool failsafeActive = true;

// --- PWM helpers ---
uint8_t toPWM(int val) {
  int pwm = map(abs(val), 0, 127, 0, 255);

  if (pwm > 0 && pwm < 40) pwm = 40;
  if (pwm > 255) pwm = 255;

  return pwm;
}

void driveMotor(int val, int chA, int chB) {
  if (abs(val) < 8) val = 0;

  uint8_t pwm = toPWM(val);

  if (val > 0) {
    ledcWrite(chA, pwm);
    ledcWrite(chB, 0);
  } else if (val < 0) {
    ledcWrite(chA, 0);
    ledcWrite(chB, pwm);
  } else {
    ledcWrite(chA, 0);
    ledcWrite(chB, 0);
  }
}

void stopAll() {
  ledcWrite(L_A_CH, 0);
  ledcWrite(L_B_CH, 0);
  ledcWrite(R_A_CH, 0);
  ledcWrite(R_B_CH, 0);
}

// --- ESP-NOW receive ---
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(ControlPacket)) {
    Serial.println("RX: bad length");
    return;
  }

  ControlPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));
  incoming.id[5] = '\0';

  if (strcmp(incoming.id, expectedID) != 0) {
    Serial.println("RX: ID mismatch");
    return;
  }

  // Clamp values (defensive)
  if (incoming.left > 127) incoming.left = 127;
  if (incoming.left < -127) incoming.left = -127;
  if (incoming.right > 127) incoming.right = 127;
  if (incoming.right < -127) incoming.right = -127;

  // Sequence check
  if (incoming.seq == lastSeq) {
    Serial.println("RX: duplicate packet");
  }
  lastSeq = incoming.seq;

  // Critical section write
  portENTER_CRITICAL_ISR(&mux);
  memcpy((void*)&current, &incoming, sizeof(ControlPacket));
  portEXIT_CRITICAL_ISR(&mux);

  lastPacketTime = millis();

  // Print minimal RX info
  Serial.printf("RX OK | L:%d R:%d Seq:%u RSSI:%d\n",
                incoming.left,
                incoming.right,
                incoming.seq,
                WiFi.RSSI());

  failsafeActive = false;
  packetCount++;
}

// --- Setup ---
void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) delay(10);

  Serial.println("Boot");

  WiFi.mode(WIFI_STA);

  Serial.print("RX MAC: ");
  Serial.println(WiFi.macAddress());

  // PWM
  ledcSetup(L_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(L_B_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_B_CH, PWM_FREQ, PWM_RES);

  ledcAttachPin(L_A, L_A_CH);
  ledcAttachPin(L_B, L_B_CH);
  ledcAttachPin(R_A, R_A_CH);
  ledcAttachPin(R_B, R_B_CH);

  stopAll();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    return;
  }

  Serial.println("ESP-NOW init OK");

  esp_now_register_recv_cb(onReceive);
}

// --- Loop ---
void loop() {
  unsigned long now = millis();

  // --- 5-second no-packet warning ---
  static unsigned long lastNoPacketPrint = 0;

  if (now - lastPacketTime >= 5000) {
    if (now - lastNoPacketPrint >= 5000) {
      Serial.println("WARN: no packets received (5s)");
      lastNoPacketPrint = now;
    }
  } else {
    // reset so it prints again on next outage
    lastNoPacketPrint = 0;
  }

  // --- Failsafe handling ---
  if (now - lastPacketTime > timeout) {
    if (!failsafeActive) {
      Serial.println("FAILSAFE: timeout");
      failsafeActive = true;
    }
    stopAll();
    delay(10);
    return;
  }

  // --- Normal operation ---
  failsafeActive = false;

  ControlPacket local;
  portENTER_CRITICAL(&mux);
  memcpy(&local, (const void*)&current, sizeof(ControlPacket));
  portEXIT_CRITICAL(&mux);

  driveMotor(local.left,  L_A_CH, L_B_CH);
  driveMotor(local.right, R_A_CH, R_B_CH);

  // --- 1 Hz status ---
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    Serial.printf("RUN | L:%d R:%d Age:%lu ms Pkts:%u\n",
                  local.left,
                  local.right,
                  now - lastPacketTime,
                  packetCount);
    lastPrint = now;
    packetCount = 0;
  }
}
