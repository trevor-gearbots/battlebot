//Receiver with one motor driver

#include <esp_now.h>
#include <WiFi.h>

// ---------------- Pins ----------------

#define L_A 32
#define L_B 33
#define R_A 25
#define R_B 26

// ---------------- PWM ----------------

#define PWM_FREQ 20000
#define PWM_RES 8

#define L_A_CH 0
#define L_B_CH 1
#define R_A_CH 2
#define R_B_CH 3

// ---------------- Packet ----------------

typedef struct {
  int8_t left;
  int8_t right;
  uint8_t seq;
} ControlPacket;

// ---------------- Shared state ----------------

volatile ControlPacket current = {0, 0, 0};

volatile uint32_t packetCount = 0;
volatile uint32_t duplicatePackets = 0;
volatile uint32_t missedPackets = 0;

volatile unsigned long lastPacketTime = 0;
volatile bool failsafeActive = true;

volatile uint8_t lastSeq = 0;
volatile bool firstPacket = true;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------- Timing ----------------

const unsigned long timeout = 200;

// ---------------- Debug ----------------

struct DebugInfo {
  int8_t left;
  int8_t right;
  uint8_t seq;
};

volatile DebugInfo dbg = {0, 0, 0};

// ---------------- PWM helpers ----------------

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
  }
  else if (val < 0) {
    ledcWrite(chA, 0);
    ledcWrite(chB, pwm);
  }
  else {
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

// ---------------- ESP-NOW RX ----------------

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(ControlPacket)) {
    return;
  }

  ControlPacket incoming;
  memcpy(&incoming, data, sizeof(incoming));

  // defensive clamp
  incoming.left = constrain(incoming.left, -127, 127);
  incoming.right = constrain(incoming.right, -127, 127);

  portENTER_CRITICAL(&mux);

  // duplicate / missed packet detection
  if (!firstPacket) {
    if (incoming.seq == lastSeq) {
      duplicatePackets++;
      portEXIT_CRITICAL(&mux);
      return;
    }

    uint8_t expected = lastSeq + 1;

    if (incoming.seq != expected) {
      missedPackets += (uint8_t)(incoming.seq - expected);
    }
  }

  firstPacket = false;
  lastSeq = incoming.seq;

  memcpy((void*)&current, &incoming, sizeof(ControlPacket));

  dbg.left = incoming.left;
  dbg.right = incoming.right;
  dbg.seq = incoming.seq;

  lastPacketTime = millis();
  failsafeActive = false;
  packetCount++;

  portEXIT_CRITICAL(&mux);
}

// ---------------- Setup ----------------

void setup() {
  Serial.begin(115200);

  unsigned long start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  Serial.println("RX Boot");

  WiFi.mode(WIFI_STA);

  Serial.print("RX MAC: ");
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

  lastPacketTime = millis();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    return;
  }

  Serial.println("ESP-NOW init OK");

  esp_now_register_recv_cb(onReceive);
}

// ---------------- Loop ----------------

void loop() {
  unsigned long now = millis();

  // ----- no packets warning -----

  static unsigned long lastNoPacketPrint = 0;

  if (now - lastPacketTime >= 5000) {
    if (now - lastNoPacketPrint >= 5000) {
      Serial.println("WARN: no packets received (5s)");
      lastNoPacketPrint = now;
    }
  }
  else {
    lastNoPacketPrint = 0;
  }

  // ----- failsafe -----

  if (now - lastPacketTime > timeout) {
    if (!failsafeActive) {
      Serial.println("FAILSAFE: timeout");
      failsafeActive = true;
    }

    stopAll();
    delay(10);
    return;
  }

  // ----- copy shared state -----

  ControlPacket local;
  DebugInfo localDbg;

  uint32_t localPackets;
  uint32_t localDupes;
  uint32_t localMissed;

  portENTER_CRITICAL(&mux);

  memcpy(&local, (const void*)&current, sizeof(ControlPacket));
  memcpy(&localDbg, (const void*)&dbg, sizeof(DebugInfo));

  localPackets = packetCount;
  localDupes = duplicatePackets;
  localMissed = missedPackets;

  portEXIT_CRITICAL(&mux);

  // ----- motor drive -----

  driveMotor(local.left, L_A_CH, L_B_CH);
  driveMotor(local.right, R_A_CH, R_B_CH);

  // ----- status -----

  static unsigned long lastPrint = 0;

  if (now - lastPrint >= 1000) {
    Serial.printf(
      "RUN | L:%d R:%d Seq:%u Age:%lu ms RX:%u DUP:%u MISS:%u\n",
      localDbg.left,
      localDbg.right,
      localDbg.seq,
      now - lastPacketTime,
      localPackets,
      localDupes,
      localMissed
    );

    portENTER_CRITICAL(&mux);

    packetCount = 0;
    duplicatePackets = 0;
    missedPackets = 0;

    portEXIT_CRITICAL(&mux);

    lastPrint = now;
  }
}
