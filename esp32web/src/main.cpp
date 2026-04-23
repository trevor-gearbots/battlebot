// ESP32 Devkit V1 with DRV8833 motor driver
// STA WiFi + WebSocket + Serial diagnostics

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// -------------------- CONFIG --------------------
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_PASSWORD"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// -------------------- GLOBALS --------------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastHeartbeatTime = 0;
const unsigned long heartbeatTimeout = 500;

String currentState = "STOP";

// -------------------- MOTOR PINS --------------------
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

// -------------------- HTML --------------------
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

.motor-status {
  margin: 30px auto;
  padding: 20px;
  border: 2px solid #ddd;
  border-radius: 10px;
  width: 320px;
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

.motor-indicator.active { background-color: #4CAF50; }

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
  <div></div><button id="fwd">F</button><div></div>
  <button id="left">L</button><button id="stop">STOP</button><button id="right">R</button>
  <div></div><button id="rev">REV</button><div></div>
</div>

<div class="motor-status">
  <div id="la-pin" class="motor-indicator"></div>
  <div id="lb-pin" class="motor-indicator"></div>
  <div id="ra-pin" class="motor-indicator"></div>
  <div id="rb-pin" class="motor-indicator"></div>

  <div style="margin-top:15px;">
    <span id="current-command">Current: STOP</span>
    <span id="wifi-status" class="wifi-status wifi-disconnected">WiFi: ?</span>
  </div>
</div>

<script>
const DIR = { STOP:0b0000,F:0b1010,B:0b0101,L:0b0110,R:0b1001 };

let ws;
let lastHeartbeat = performance.now();

function setPin(id,v){
  document.getElementById(id).classList.toggle("active",v);
}

function setDirection(dir){
  setPin("la-pin",(dir>>3)&1);
  setPin("lb-pin",(dir>>2)&1);
  setPin("ra-pin",(dir>>1)&1);
  setPin("rb-pin",(dir>>0)&1);
}

function updateWiFi(ok){
  const el=document.getElementById("wifi-status");
  el.textContent = ok ? "WiFi: Connected" : "WiFi: Lost";
  el.className = "wifi-status " + (ok?"wifi-connected":"wifi-disconnected");
}

function send(cmd){
  if(ws && ws.readyState===1) ws.send(cmd);
}

function connect(){
  ws = new WebSocket("ws://"+location.host+"/ws");

  ws.onopen = ()=>updateWiFi(true);

  ws.onmessage = (e)=>{
    if(e.data==="PONG"){
      lastHeartbeat=performance.now();
      return;
    }
    if(e.data==="WIFI_OK"){ updateWiFi(true); return; }
    if(e.data==="WIFI_LOST"){ updateWiFi(false); return; }

    document.getElementById("current-command").textContent="Current: "+e.data;
    setDirection(DIR[e.data] ?? DIR.STOP);
  };

  ws.onclose = ()=>{ updateWiFi(false); setTimeout(connect,1000); };
  ws.onerror = ()=>updateWiFi(false);
}

connect();

setInterval(()=>{
  if(ws && ws.readyState===1) ws.send("PING");
  if(performance.now()-lastHeartbeat>500) updateWiFi(false);
},100);

["fwd","rev","left","right","stop"].forEach(id=>{
  document.getElementById(id).onmousedown=()=>{
    const map={fwd:"F",rev:"B",left:"L",right:"R",stop:"STOP"};
    send(map[id]);
  };
});
</script>

</body>
</html>
)rawliteral";

// -------------------- MOTOR --------------------
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
  WiFi.begin(ssid, password);

  Serial.println("[WiFi] Connecting...");

  unsigned long startAttempt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
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
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    client->text("WIFI_OK");
    client->text(currentState);
  }

  else if (type == WS_EVT_DATA) {
    String msg;
    msg.reserve(len);
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

// -------------------- SETUP --------------------
void setup() {
  Serial.begin(115200);

  pinMode(LeftA, OUTPUT);
  pinMode(LeftB, OUTPUT);
  pinMode(RightA, OUTPUT);
  pinMode(RightB, OUTPUT);

  setDirection(DIR_STOP);
  lastHeartbeatTime = millis();

  WiFi.onEvent(onWiFiEvent);
  startWiFi();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", index_html);
  });

  server.begin();
}

// -------------------- LOOP --------------------
void loop() {
  ws.cleanupClients();

  static unsigned long lastReconnectAttempt = 0;
  static unsigned long lastRSSI = 0;

  // Reconnect logic
  if (WiFi.status() != WL_CONNECTED) {

    if (millis() - lastReconnectAttempt > 5000) {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      lastReconnectAttempt = millis();
    }

    if (currentState != "STOP") {
      Serial.println("[FAILSAFE] WiFi lost -> STOP");
      setDirection(DIR_STOP);
      currentState = "STOP";
    }

    return;
  }

  // Periodic RSSI report
  if (millis() - lastRSSI > 5000) {
    Serial.print("[WiFi] RSSI: ");
    Serial.println(WiFi.RSSI());
    lastRSSI = millis();
  }

  // Heartbeat fail-safe
  unsigned long now = millis();
  if (now - lastHeartbeatTime > heartbeatTimeout) {
    if (currentState != "STOP") {
      Serial.println("[FAILSAFE] Heartbeat timeout -> STOP");
      setDirection(DIR_STOP);
      currentState = "STOP";
      ws.textAll("STOP");
    }
  }
}
