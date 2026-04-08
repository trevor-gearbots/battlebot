//Receiver

#include <esp_now.h>
#include <WiFi.h>

// Motor pins
#define L_A 25
#define L_B 26
#define R_A 27
#define R_B 14

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
} ControlPacket;

// Keep volatile (safe for callback updates)
volatile ControlPacket current;

unsigned long lastPacketTime = 0;
const int timeout = 200;

// Convert -127..127 → 0..255
uint8_t toPWM(int val) {
  int pwm = map(abs(val), 0, 127, 0, 255);

  // Minimum threshold to overcome motor stall
  if (pwm > 0 && pwm < 40) pwm = 40;

  return pwm;
}

void driveMotor(int val, int chA, int chB) {
  // Deadband
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

// ESP-NOW receive callback (compatible with older core)
void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(ControlPacket)) return;

  ControlPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  // Ensure null termination
  incoming.id[5] = '\0';

  // Filter by ID
  if (strcmp(incoming.id, expectedID) != 0) return;

  // FIX: cannot assign to volatile → use memcpy
  memcpy((void*)&current, &incoming, sizeof(ControlPacket));

  lastPacketTime = millis();
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("RX MAC: ");
  Serial.println(WiFi.macAddress());

  // Setup PWM
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
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onReceive);
}

void loop() {
  // Failsafe
  if (millis() - lastPacketTime > timeout) {
    stopAll();
    return;
  }

  // Read volatile safely (copy to local)
  ControlPacket local;
  memcpy(&local, (const void*)&current, sizeof(ControlPacket));

  driveMotor(local.left,  L_A_CH, L_B_CH);
  driveMotor(local.right, R_A_CH, R_B_CH);
}
