// ESP32 Devkit V1 with DRV8833 motor driver
// Continuous command streaming + short motor failsafe

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_wifi.h"

// -------------------- CONFIG --------------------
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* CODE_VERSION = "v0.31";

// -------------------- GLOBALS --------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastCommandTime = 0;
const unsigned long commandTimeout = 300;

char currentState[8] = "STOP";

// -------------------- MOTOR PINS --------------------
#define LeftA  32
#define LeftB  33
#define RightA 25
#define RightB 26

// Bit Order: LA LB RA RB
#define DIR_STOP  0b0000
#define DIR_FWD   0b1010
#define DIR_BACK   0b0101
#define DIR_LEFT  0b0110
#define DIR_RIGHT 0b1001

// -------------------- HTML --------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>
body {
  font-family: sans-serif;
  text-align: center;
}

.grid {
  display: grid;
  grid-template-columns: 80px 80px 80px;
  grid-template-rows: 80px 80px 80px;
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
  transition: background-color 0.05s;
}

button.active {
  background-color: #4CAF50;
  color: white;
}

.motor-status {
  margin: 20px auto;
  padding: 20px;
  border: 2px solid #ddd;
  border-radius: 10px;
  width: 320px;
  background-color: #f9f9f9;
}

.motor-indicator {
  display: inline-block;
  width: 30px;
  height: 30px;
  margin: 5px;
  border-radius: 50%;
  background-color: #ccc;
}

.motor-indicator.active {
  background-color: #4CAF50;
}

.wifi-status {
  margin-left: 10px;
  padding: 5px 10px;
  border-radius: 5px;
  font-size: 12px;
}

.wifi-connected {
  background-color: #4CAF50;
  color: white;
}

.wifi-disconnected {
  background-color: #f44336;
  color: white;
}
</style>
</head>

<body>

<h3>ESP32 Motor Control</h3>
<div>Code Version: %VERSION%</div>

<div class="grid">
  <div></div><button id="fwd">F</button><div></div>
  <button id="left">L</button><button id="stop">STOP</button><button id="right">R</button>
  <div></div><button id="back">B</button><div></div>
</div>

<div class="motor-status">

  <div id="la-pin" class="motor-indicator"></div>
  <div id="lb-pin" class="motor-indicator"></div>
  <div id="ra-pin" class="motor-indicator"></div>
  <div id="rb-pin" class="motor-indicator"></div>

  <div style="margin-top:20px;">
    <div>ESP32 Motor State</div>

    <div id="esp-la-pin" class="motor-indicator"></div>
    <div id="esp-lb-pin" class="motor-indicator"></div>
    <div id="esp-ra-pin" class="motor-indicator"></div>
    <div id="esp-rb-pin" class="motor-indicator"></div>
  </div>

 <div style="
  margin-top:15px;
  display:flex;
  justify-content:center;
  align-items:flex-start;
  gap:20px;
">

  <div style="
    width:120px;
    text-align:center;
  ">
    <div>Command</div>
    <div id="current-command">STOP</div>
  </div>

  <div style="
    width:120px;
    text-align:center;
  ">
    <div>WiFi</div>
    <div id="wifi-status"
      class="wifi-status wifi-disconnected">
      ?
    </div>
  </div>

  <div style="
    width:120px;
    text-align:center;
  ">
    <div>Latency</div>
    <div id="latency-status">-- ms</div>
  </div>

</div>

</div>

<script>

const DIR = {
  STOP:0b0000,
  F:0b1010,
  B:0b0101,
  L:0b0110,
  R:0b1001
};

let lastRxTime = Infinity;
let pingStartTime = 0;
let latencyMs = 0;

let ws;
let activeCommand = "STOP";

function setPin(id,v){
  document.getElementById(id).classList.toggle("active",v);
}

function setDirection(dir){
  setPin("la-pin",(dir>>3)&1);
  setPin("lb-pin",(dir>>2)&1);
  setPin("ra-pin",(dir>>1)&1);
  setPin("rb-pin",(dir>>0)&1);
}

function setEspDirection(dir){
  setPin("esp-la-pin",(dir>>3)&1);
  setPin("esp-lb-pin",(dir>>2)&1);
  setPin("esp-ra-pin",(dir>>1)&1);
  setPin("esp-rb-pin",(dir>>0)&1);
}

function updateLatency(ms){
  latencyMs = ms;
  document.getElementById("latency-status").textContent =
    ms + " ms";;
}

function updateWiFi(ok){
  const el=document.getElementById("wifi-status");
  el.textContent = ok ? "Connected" : "Lost";
  el.className = "wifi-status " + (ok?"wifi-connected":"wifi-disconnected");
}

function send(cmd){
  if(ws && ws.readyState===1){
    ws.send(cmd);
  }
}

function updateButtonHighlight(cmd){

  Object.values(commandButtons).forEach(id=>{
    document.getElementById(id).classList.remove("active");
  });

  const activeId = commandButtons[cmd];

  if (activeId) {
    document.getElementById(activeId).classList.add("active");
  }
}

function setCommand(cmd){
  activeCommand = cmd;

  updateButtonHighlight(cmd);

document.getElementById("current-command").textContent = cmd;

  setDirection(DIR[cmd] ?? DIR.STOP);
}

function connect(){

  ws = new WebSocket("ws://" + location.host + "/ws");

  ws.onopen = ()=>{
    lastRxTime = performance.now();
    activeCommand = "STOP";
    updateButtonHighlight("STOP");
    setDirection(DIR.STOP);
    send("STOP");
    updateWiFi(true);
  };

  ws.onmessage = (e)=>{
    lastRxTime = performance.now();
    if(e.data.startsWith("PING:")){
      lastPongTime = performance.now();
      updateLatency(
        Math.round(performance.now() - pingStartTime)
      );
  return;
  }

    if(e.data==="WIFI_OK"){
      updateWiFi(true);
      return;
    }

    if(e.data==="WIFI_LOST"){
      updateWiFi(false);
      return;
    }

  document.getElementById("current-command").textContent = e.data;

    setEspDirection(DIR[e.data] ?? DIR.STOP);
  };

  ws.onclose = ()=>{
    activeCommand = "STOP";
    updateButtonHighlight("STOP");
    setDirection(DIR.STOP);
    updateWiFi(false);
    setTimeout(connect,1000);
  };

  ws.onerror = ()=>{
    updateWiFi(false);
  };
}

connect();

setInterval(()=>{
  const connected =
    ws &&
    ws.readyState === 1 &&
    (performance.now() - lastPongTime < 1500);
  updateWiFi(connected);
},250);

// Continuously transmit active command
let pingId = 0;
let lastPongTime = 0;

setInterval(()=>{
  send(activeCommand);
},100);

setInterval(()=>{
  if(ws && ws.readyState === 1){
    pingId++;
    pingStartTime = performance.now();
    ws.send("PING:" + pingId);
  }
},500);

// -------------------- INPUT --------------------

const keyToCommand = {
  "w":"F",
  "ArrowUp":"F",

  "s":"B",
  "ArrowDown":"B",

  "a":"L",
  "ArrowLeft":"L",

  "d":"R",
  "ArrowRight":"R",

  "x":"STOP",
  " ":"STOP"
};

const commandButtons = {
  "F":"fwd",
  "B":"back",
  "L":"left",
  "R":"right",
  "STOP":"stop"
};

function bindButton(id,cmd){
  const el=document.getElementById(id);
  el.onmousedown = ()=> setCommand(cmd);
  el.onmouseup = ()=> setCommand("STOP");
  el.onmouseleave = ()=> setCommand("STOP");
  el.ontouchstart = (e)=>{
    e.preventDefault();
    setCommand(cmd);
  };

  el.ontouchend = ()=>{
    setCommand("STOP");
  };

  el.ontouchcancel = ()=>{
    setCommand("STOP");
  };
}

bindButton("fwd","F");
bindButton("back","B");
bindButton("left","L");
bindButton("right","R");

document.getElementById("stop").onclick = ()=>{
  setCommand("STOP");
};

// Keyboard controls
const pressedKeys = new Set();

function updateKeyboardCommand(){

  if (pressedKeys.has("x") || pressedKeys.has(" ")) {
    setCommand("STOP");
    return;
  }

  if (pressedKeys.has("w") || pressedKeys.has("ArrowUp")) {
    setCommand("F");
    return;
  }

  if (pressedKeys.has("s") || pressedKeys.has("ArrowDown")) {
    setCommand("B");
    return;
  }

  if (pressedKeys.has("a") || pressedKeys.has("ArrowLeft")) {
    setCommand("L");
    return;
  }

  if (pressedKeys.has("d") || pressedKeys.has("ArrowRight")) {
    setCommand("R");
    return;
  }

  setCommand("STOP");
}

document.addEventListener("keydown",(e)=>{
  if (keyToCommand[e.key] !== undefined) {
    e.preventDefault();
    pressedKeys.add(e.key);
    updateKeyboardCommand();
  }
});

document.addEventListener("keyup",(e)=>{
  if (keyToCommand[e.key] !== undefined) {
    e.preventDefault();
    pressedKeys.delete(e.key);
    updateKeyboardCommand();
  }
});

window.addEventListener("blur",()=>{
  pressedKeys.clear();
  setCommand("STOP");
});

window.addEventListener("offline",()=>{
  updateWiFi(false);
});

window.addEventListener("online",()=>{
  connect();
});

</script>

</body>
</html>
)rawliteral";

// -------------------- MOTOR --------------------
void setDirection(uint8_t dir) {
  digitalWrite(LeftA,  (dir >> 3) & 1);
  digitalWrite(LeftB,  (dir >> 2) & 1);
  digitalWrite(RightA, (dir >> 1) & 1);
  digitalWrite(RightB, (dir >> 0) & 1);
}

void handleCommand(const char* msg) {
  lastCommandTime = millis();
  if (strcmp(currentState, msg) == 0) {
    return;
  }

  strncpy(currentState, msg, sizeof(currentState) - 1);
  currentState[sizeof(currentState) - 1] = '\0';

  uint8_t dir = DIR_STOP;

  if      (strcmp(msg, "F") == 0)    dir = DIR_FWD;
  else if (strcmp(msg, "B") == 0)    dir = DIR_BACK;
  else if (strcmp(msg, "L") == 0)    dir = DIR_LEFT;
  else if (strcmp(msg, "R") == 0)    dir = DIR_RIGHT;

  setDirection(dir);
  ws.textAll(currentState);
}

// -------------------- WIFI EVENTS --------------------
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WiFi] STA started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WiFi] Connected to AP");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      ws.textAll("WIFI_OK");
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WiFi] Disconnected");
      ws.textAll("WIFI_LOST");
      break;

    default:
      break;
  }
}

// -------------------- WIFI START --------------------
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());
  WiFi.begin(ssid, password);
  Serial.println("[WiFi] Connecting...");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < 8000) {
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected");
  } else {
    Serial.println("\n[WiFi] Connection timeout");
  }
}

// -------------------- WEBSOCKET --------------------
void onWsEvent(AsyncWebSocket *server,
               AsyncWebSocketClient *client,
               AwsEventType type,
               void *arg,
               uint8_t *data,
               size_t len) {

  if (type == WS_EVT_CONNECT) {
    client->text("WIFI_OK");
    client->text(currentState);
  }

  else if (type == WS_EVT_DATA) {
    char msg[8] = {0};
    if (len >= sizeof(msg)) {
      return;
    }

    memcpy(msg, data, len);
    msg[len] = '\0';

    if (strncmp(msg, "PING:", 5) == 0) {
      client->text(msg);
      return;
    }

    if (strcmp(msg, "F") == 0 ||
        strcmp(msg, "B") == 0 ||
        strcmp(msg, "L") == 0 ||
        strcmp(msg, "R") == 0 ||
        strcmp(msg, "STOP") == 0) {

      handleCommand(msg);
    }
  }
}

// -------------------- SETUP --------------------
void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.print("ESP32 Motor Controller ");
  Serial.println(CODE_VERSION);

  pinMode(LeftA, OUTPUT);
  pinMode(LeftB, OUTPUT);
  pinMode(RightA, OUTPUT);
  pinMode(RightB, OUTPUT);

  setDirection(DIR_STOP);
  lastCommandTime = millis();
  WiFi.onEvent(onWiFiEvent);
  startWiFi();
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = index_html;
    html.replace("%VERSION%", CODE_VERSION);
    request->send(200, "text/html", html);
  });

  server.begin();
}

// -------------------- LOOP --------------------
void loop() {
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long lastRSSI = 0;
  static unsigned long lastCleanup = 0;

  // Cleanup websocket clients
  if (millis() - lastCleanup > 1000) {
    ws.cleanupClients();
    lastCleanup = millis();
  }

  // Reconnect logic
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastReconnectAttempt > 15000) {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.reconnect();
      lastReconnectAttempt = millis();
    }

    if (strcmp(currentState, "STOP") != 0) {
      Serial.println("[FAILSAFE] WiFi lost -> STOP");
      setDirection(DIR_STOP);
      strcpy(currentState, "STOP");
    }

    return;
  }

  // RSSI report
  if (millis() - lastRSSI > 5000) {
    Serial.print("[WiFi] RSSI: ");
    Serial.println(WiFi.RSSI());
    lastRSSI = millis();
  }

  // Command timeout fail-safe
  if (millis() - lastCommandTime > commandTimeout) {
    if (strcmp(currentState, "STOP") != 0) {
      Serial.println("[FAILSAFE] Command timeout -> STOP");
      setDirection(DIR_STOP);
      strcpy(currentState, "STOP");
      ws.textAll("STOP");
    }
  }


}
