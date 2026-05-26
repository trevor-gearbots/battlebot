//use single joystick to control brightness of LED's in same pattern used for motor control
//input mixing, switch on/off, debounce
//not tested
//to-do: revisit input mixing for circular mapping, input timing loop

#include <Arduino.h>

const int ledIN1 = 25;
const int ledIN2 = 26;
const int ledIN3 = 32;
const int ledIN4 = 33;

const int joyX = 34;
const int joyY = 35;
const int joySW = 4;

const int pwmFreq = 5000;
const int pwmResolution = 8;

const int channel1 = 0;
const int channel2 = 1;
const int channel3 = 2;
const int channel4 = 3;

bool enabled = false;

bool buttonState = HIGH;
bool lastReading = HIGH;

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 30;

void handleButton();
void handleJoystick();
void setOutputs(int a, int b, int c, int d);




void handleButton() {
  bool reading = digitalRead(joySW);

  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;

      if (buttonState == LOW) {
        enabled = !enabled;
      }
    }
  }

  lastReading = reading;
}

void handleJoystick() {
  if (!enabled) {
    setOutputs(0, 0, 0, 0);
    return;
  }

  int rawX = analogRead(joyX);
  int rawY = analogRead(joyY);

  float x = map(rawX, 0, 4095, -255, 255) / 255.0;
  float y = map(rawY, 0, 4095, -255, 255) / 255.0;

  if (abs(x) < 0.08) x = 0;
  if (abs(y) < 0.08) y = 0;

  float left = constrain(y + x, -1.0, 1.0);
  float right = constrain(y - x, -1.0, 1.0);

  int in1 = 0;
  int in2 = 0;
  int in3 = 0;
  int in4 = 0;

  if (left > 0) {
    in1 = left * 255;
  } else {
    in2 = -left * 255;
  }

  if (right > 0) {
    in3 = right * 255;
  } else {
    in4 = -right * 255;
  }

  setOutputs(in1, in2, in3, in4);
}

void setOutputs(int a, int b, int c, int d) {
  ledcWrite(channel1, a);
  ledcWrite(channel2, b);
  ledcWrite(channel3, c);
  ledcWrite(channel4, d);
}

void setup() {
  pinMode(joySW, INPUT_PULLUP);

  ledcSetup(channel1, pwmFreq, pwmResolution);
  ledcSetup(channel2, pwmFreq, pwmResolution);
  ledcSetup(channel3, pwmFreq, pwmResolution);
  ledcSetup(channel4, pwmFreq, pwmResolution);

  ledcAttachPin(ledIN1, channel1);
  ledcAttachPin(ledIN2, channel2);
  ledcAttachPin(ledIN3, channel3);
  ledcAttachPin(ledIN4, channel4);

  setOutputs(0, 0, 0, 0);
}

void loop() {
  handleButton();
  handleJoystick();
}
