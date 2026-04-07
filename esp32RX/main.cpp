#include <esp_now.h>
#include <WiFi.h>

// Motor pins
#define L_A 32
#define L_B 33
#define R_A 25
#define R_B 26

// PWM settings
#define PWM_FREQ 20000   // 20 kHz (quiet for motors)
#define PWM_RES 8        // 8-bit (0–255)

// PWM channels
#define L_A_CH 0
#define L_B_CH 1
#define R_A_CH 2
#define R_B_CH 3

typedef struct {
  int8_t left;
  int8_t right;
} ControlPacket;

volatile ControlPacket current;
unsigned long lastPacketTime = 0;
const int timeout = 200;

// Convert -127..127 → 0..255
uint8_t toPWM(int val) {
  return map(abs(val), 0, 127, 0, 255);
}

void driveMotor(int val, int chA, int chB) {
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

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len == sizeof(ControlPacket)) {
    memcpy((void*)&current, data, sizeof(ControlPacket));
    lastPacketTime = millis();
  }
}

void setup() {
  WiFi.mode(WIFI_STA);

  // Setup PWM channels
  ledcSetup(L_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(L_B_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_B_CH, PWM_FREQ, PWM_RES);

  ledcAttachPin(L_A, L_A_CH);
  ledcAttachPin(L_B, L_B_CH);
  ledcAttachPin(R_A, R_A_CH);
  ledcAttachPin(R_B, R_B_CH);

  if (esp_now_init() != ESP_OK) return;

  esp_now_register_recv_cb(onReceive);
}

void loop() {
  // Failsafe
  if (millis() - lastPacketTime > timeout) {
    stopAll();
    return;
  }

  driveMotor(current.left,  L_A_CH, L_B_CH);
  driveMotor(current.right, R_A_CH, R_B_CH);
}
