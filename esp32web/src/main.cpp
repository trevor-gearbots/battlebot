// ESP32 Devkit V1 with DRV8833 motor driver, web page


#include <WiFi.h>
#include <ESPAsyncWebServer.h>

unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatInterval = 100; // ms (server not used anymore, kept for tuning)
const unsigned long heartbeatTimeout = 500;  // ms

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Motor pins
#define LeftA  32
#define LeftB  33
#define RightA 25
#define RightB 26

// Bit Order: LA LB RA RB
#define DIR_STOP  0b0000
#define DIR_FWD   0b1010
#define DIR_REV   0b0101
#define DIR_LEFT  0b0110
#define DIR_RIGHT 0b1001

String currentState = "STOP";

// ---- Embedded webpage ----
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { font-family: sans-serif; text-align: center; }

  .grid {
    display: grid;
    grid-template-columns: 100px 100px 100px;
    grid-template-rows: 100px 100px 100px;
    gap: 10px;
    justify-content: center;
    margin-top: 30px;
  }

  button {
    font-size: 16px;
    background-color: #ccc;
    border: none;
    border-radius: 5px;
    cursor: pointer;
  }

  .pressed { background-color: #4CAF50; color: white; }
  .stop-active { background-color: #f44336; color: white; }

  .motor-status {
    margin: 30px auto;
    padding: 20px;
    border: 2px solid #ddd;
    border-radius: 10px;
    width: 300px;
    background-color: #f9f9f9;
  }

  .motor-indicator {
    display: inline-block;
    width: 40px;
    height: 40px;
    margin: 5px;
    border-radius: 50%;
    background-color: #ccc;
  }

  .motor-indicator.active {
    background-color: #4CAF50;
  }

  .motor-label {
    font-size: 12px;
    font-weight: bold;
    color: white;
  }

  .status-text {
    margin-top: 15px;
    font-weight: bold;
  }

  .wifi-status {
    margin-left: 10px;
    padding: 5px 10px;
    border-radius: 5px;
    font-size: 12px;
  }

  .wifi-connected { background-color: #4CAF50; color: white; }
  .wifi-disconnected { background-color: #f44336; color: white; }

</style>
</head>
<body>

<h2>ESP32 Motor Control</h2>

<div class="grid">
  <div></div>
  <button id="fwd">F</button>
  <div></div>

  <button id="left">L</button>
  <button id="stop">STOP</button>
  <button id="right">R</button>

  <div></div>
  <button id="rev">REV</button>
  <div></div>
</div>

<div class="motor-status">
  <h3>Pin Status</h3>
  <div class="motor-indicator" id="la-pin">LA</div>
  <div class="motor-indicator" id="lb-pin">LB</div>
  <div class="motor-indicator" id="ra-pin">RA</div>
  <div class="motor-indicator" id="rb-pin">RB</div>

  <div class="status-text">
    <span id="current-command">Current: STOP</span>
    <span id="wifi-status" class="wifi-status wifi-disconnected">WiFi: Disconnected</span>
  </div>
</div>

<script>

const DIR = {
  STOP: 0b0000,
  F:    0b1010,
  B:    0b0101,
  L:    0b0110,
  R:    0b1001
};

let ws;
let lastHeartbeat = performance.now();
const heartbeatTimeout = 500;

function setPin(id, v) {
  document.getElementById(id).classList.toggle("active", v);
}

function setDirection(dir) {
  setPin("la-pin", (dir>>3)&1);
  setPin("lb-pin", (dir>>2)&1);
  setPin("ra-pin", (dir>>1)&1);
  setPin("rb-pin", (dir>>0)&1);
}

function send(cmd) {
  if (ws && ws.readyState === 1) ws.send(cmd);
}

function updateWiFi(ok) {
  const el = document.getElementById("wifi-status");
  el.textContent = ok ? "WiFi: Connected" : "WiFi: Disconnected";
  el.className = "wifi-status " + (ok ? "wifi-connected" : "wifi-disconnected");
}

function connect() {
  ws = new WebSocket("ws://" + location.host + "/ws");

  ws.onopen = () => updateWiFi(true);

  ws.onmessage = (e) => {
    if (e.data === "PONG") {
      lastHeartbeat = performance.now();
      return;
    }
    document.getElementById("current-command").textContent = "Current: " + e.data;
    setDirection(DIR[e.data] ?? DIR.STOP);
  };

  ws.onclose = () => {
    updateWiFi(false);
    setTimeout(connect, 1000);
  };

  ws.onerror = () => updateWiFi(false);
}

connect();

// heartbeat
setInterval(() => {
  if (ws && ws.readyState === 1) {
    ws.send("PING");
  }
  if (performance.now() - lastHeartbeat > heartbeatTimeout) {
    updateWiFi(false);
  }
}, 100);

// controls
document.getElementById("fwd").onmousedown = ()=>send("F");
document.getElementById("rev").onmousedown = ()=>send("B");
document.getElementById("left").onmousedown = ()=>send("L");
document.getElementById("right").onmousedown = ()=>send("R");
document.getElementById("stop").onmousedown = ()=>send("STOP");

</script>
</body>
</html>
)rawliteral";

// ---- Motor control ----
void setDirection(uint8_t dir) {
  digitalWrite(LeftA, (dir >> 3) & 1);
  digitalWrite(LeftB, (dir >> 2) & 1);
  digitalWrite(RightA, (dir >> 1) & 1);
  digitalWrite(RightB, (dir >> 0) & 1);
}

void handleCommand(String msg) {
  if (currentState == msg) return;

  currentState = msg;

  uint8_t dir = DIR_STOP;
  if (msg == "F") dir = DIR_FWD;
  else if (msg == "B") dir = DIR_REV;
  else if (msg == "L") dir = DIR_LEFT;
  else if (msg == "R") dir = DIR_RIGHT;

  setDirection(dir);
  ws.textAll(currentState);
}

// ---- WebSocket ----
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    client->text(currentState);
  }

  if (type == WS_EVT_DATA) {
    String msg;
    for (size_t i = 0; i < len; i++) msg += (char)data[i];

    if (msg == "PING") {
      lastHeartbeatTime = millis();
      client->text("PONG");
      return;
    }

    if (msg == "F" || msg == "B" || msg == "L" || msg == "R" || msg == "STOP") {
      lastHeartbeatTime = millis();
      handleCommand(msg);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LeftA, OUTPUT);
  pinMode(LeftB, OUTPUT);
  pinMode(RightA, OUTPUT);
  pinMode(RightB, OUTPUT);

  setDirection(DIR_STOP);
  lastHeartbeatTime = millis();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }

  // Wait a moment for IP assignment to stabilize
  delay(1000);
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.begin();
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    if (currentState != "STOP") {
      setDirection(DIR_STOP);
      currentState = "STOP";
    }
    return;
  }

  if (now - lastHeartbeatTime > heartbeatTimeout) {
    if (currentState != "STOP") {
      setDirection(DIR_STOP);
      currentState = "STOP";
    }
  }
}
