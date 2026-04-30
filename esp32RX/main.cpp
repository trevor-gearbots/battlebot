//Receiver

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
} ControlPacket;

volatile ControlPacket current = {"NONE", 0, 0};

unsigned long lastPacketTime = 0;
const int timeout = 200;

// Debug control
bool failsafeActive = true;
unsigned long lastDebugPrint = 0;

// ---------- Helpers ----------

void printMAC(const uint8_t *mac) {
  char buf[18];
  snprintf(buf, sizeof(buf),
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2],
           mac[3], mac[4], mac[5]);
  Serial.print(buf);
}

// Convert -127..127 → 0..255
uint8_t toPWM(int val) {
  int pwm = map(abs(val), 0, 127, 0, 255);
  if (pwm > 0 && pwm < 40) pwm = 40;
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

// ---------- ESP-NOW callback ----------

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {

  if (len != sizeof(ControlPacket)) {
    Serial.print("Packet rejected (len=");
    Serial.print(len);
    Serial.println(")");
    return;
  }

  ControlPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));
  incoming.id[5] = '\0';

  if (strcmp(incoming.id, expectedID) != 0) {
    Serial.print("Packet rejected (ID mismatch: ");
    Serial.print(incoming.id);
    Serial.println(")");
    return;
  }

  memcpy((void*)&current, &incoming, sizeof(ControlPacket));
  lastPacketTime = millis();

  // Light debug (safe enough if not too frequent)
  Serial.print("RX OK from ");
  printMAC(mac);
  Serial.print(" | L=");
  Serial.print(incoming.left);
  Serial.print(" R=");
  Serial.println(incoming.right);
}

// ---------- Setup ----------

void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial) {
    if (millis() - start > 5000) break;
    delay(10);
  }

  Serial.println("\nBooting receiver...");

  WiFi.mode(WIFI_STA);

  Serial.print("STA MAC: ");
  Serial.println(WiFi.macAddress());

  // PWM setup
  ledcSetup(L_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(L_B_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_A_CH, PWM_FREQ, PWM_RES);
  ledcSetup(R_B_CH, PWM_FREQ, PWM_RES);

  ledcAttachPin(L_A, L_A_CH);
  ledcAttachPin(L_B, L_B_CH);
  ledcAttachPin(R_A, R_A_CH);
  ledcAttachPin(R_B, R_B_CH);

  stopAll();
  Serial.println("PWM initialized");

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: ESP-NOW init failed");
    return;
  }

  Serial.println("ESP-NOW initialized");

  esp_now_register_recv_cb(onReceive);
  Serial.println("Receive callback registered");
}

// ---------- Loop ----------

void loop() {

  // Failsafe handling
  if (millis() - lastPacketTime > timeout) {
    if (!failsafeActive) {
      Serial.println("FAILSAFE: signal lost, stopping motors");
      failsafeActive = true;
    }
    stopAll();
    return;
  }

  if (failsafeActive) {
    Serial.println("Signal restored");
    failsafeActive = false;
  }

  ControlPacket local;
  memcpy(&local, (const void*)&current, sizeof(ControlPacket));

  driveMotor(local.left,  L_A_CH, L_B_CH);
  driveMotor(local.right, R_A_CH, R_B_CH);

  // Rate-limited debug output (every 250 ms)
  if (millis() - lastDebugPrint > 250) {
    lastDebugPrint = millis();

    Serial.print("Drive | L=");
    Serial.print(local.left);
    Serial.print(" R=");
    Serial.println(local.right);
  }
}
