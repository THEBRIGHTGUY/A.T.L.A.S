// THE BRAIN (Heltec 1): receives the sensor board's readings over LoRa, runs your AI model,
// serves the offline phone app from its own WiFi, and sends the data to the cloud.
// Libraries to install: RadioLib and PubSubClient (by Nick O'Leary).
// Keep these files in this folder: common.h, radio_setup.h, webpage.h, growther_model.h

#include "common.h"

// ================= SETTINGS FOR THIS BOARD =================
const char* AP_SSID = "SoilGateway";       // the gateway's OWN WiFi, works with no internet
const char* AP_PASS = "atlas-soil-2026";         // at least 8 characters
#define CLOUD_ENABLED 1                    // 0 = local only
const char* WIFI_SSID = "your-wifi-name";  // your router WiFi, used for the cloud ("" to skip)
const char* WIFI_PASS = "your-wifi-password";
const char* MQTT_HOST = "broker.emqx.io";
#define MQTT_PORT 1883         // 1883 = plain, 8883 = encrypted
#define MQTT_TLS  0            // 1 if you use port 8883
const char* MQTT_USER = "";    // leave empty for public brokers
const char* MQTT_PASS = "";
#define PH_DIV 10.0            // pH register / 10 (some sensors use 100)
// ===========================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "webpage.h"
#include "growther_model.h"   // your trained model, made by convert_onnx.py

#if MQTT_TLS
WiFiClientSecure net;
#else
WiFiClient net;
#endif
PubSubClient mqtt(net);
WebServer server(80);
volatile bool rxFlag = false;
unsigned long lastMqttTry = 0;

// newest reading of each node, kept in RAM for the local app
struct Latest { char id[7]; String json; unsigned long ms; };
Latest latest[8];
int nLatest = 0;

void IRAM_ATTR onRx() { rxFlag = true; }

// ================= YOUR MODEL GOES HERE =================
// Called once for every new reading that reaches the brain.
struct Reading {
  bool hasSoil, hasLight, hasAir;            // which sensors answered this time
  float moist, soilT, ec, ph, n, p, k;       // soil: %, C, uS/cm, pH, mg/kg
  float lux;                                 // light
  float airT, airH, pres;                    // air: C, %, hPa
};

// --- the model's inputs ---
// Growther.onnx expects GROWTHER_NUM_OBS (75) numbers. The Unity code shown so far only lists 13 of them,
// so the other 62 are still 0 below. Until all 75 are filled in correctly, NO AI result is sent to the apps.
#define AI_INPUTS_COMPLETE 0     // set to 1 ONLY when buildObservations() fills all 75 inputs correctly

// Inputs we have NO sensor for. The model was trained with them, so set sensible values here.
static float WATER_TEMP = 0;      // water temperature, C
static float WATER_QUALITY = 0;   // water quality, 0-100
static float WATER_FLOW = 0;      // water flow rate
static bool  VALVE_OPEN = false;  // update this when you add a valve or relay

// The Unity code has ONE "NPK" value. How to get it from our three readings:
#define NPK_MODE 0                // 0 = average of N, P, K   1 = sum of N, P, K

void buildObservations(const Reading &r, float *o) {
  for (int i = 0; i < GROWTHER_NUM_OBS; i++) o[i] = 0;     // everything not known yet
#if NPK_MODE == 1
  float npk = r.n + r.p + r.k;
#else
  float npk = (r.n + r.p + r.k) / 3.0f;
#endif
  // ASSUMPTION: these 13 are the first 13 inputs of one frame. Check against the full Unity code.
  o[0]  = r.moist / 100.0f;       // SoilMoisturePercentage / 100
  o[1]  = r.soilT / 80.0f;        // SoilTemperature / 80
  o[2]  = r.ec / 10000.0f;        // ElectricalConductivity / 10000
  o[3]  = r.ph / 14.0f;           // Ph / 14
  o[4]  = npk / 1999.0f;          // NPK / 1999
  o[5]  = WATER_TEMP / 100.0f;    // WaterTemperature / 100
  o[6]  = WATER_QUALITY / 100.0f; // WaterQuality / 100
  o[7]  = r.lux / 70000.0f;       // LightIntensity / 70000
  o[8]  = r.airT / 100.0f;        // Temperature / 100
  o[9]  = r.airH / 100.0f;        // Humidity / 100
  o[10] = r.pres / 1100.0f;       // Pressure / 1100
  o[11] = WATER_FLOW / 100.0f;    // WaterFlowRate / 100
  o[12] = VALVE_OPEN ? 1.0f : 0.0f; // isValueOpen ? 1 : 0
  // TODO: o[13] ... o[74]
}

// What the model's answers mean. ASSUMPTIONS from the WheatAgent description: check against your Unity code.
//   continuous action (-1..1) -> irrigation flow 0..5 as (a + 1) / 2 * 5
//   3 yes/no choices          -> [0] add NPK   [1] pH up   [2] pH down
#define ACT_IRRIGATION_MAX 5.0f
static String describeActions(float irr, const int *d) {
  char buf[64];
  snprintf(buf, sizeof buf, "Irrigation %.1f/%.0f%s%s%s", (irr + 1.0f) / 2.0f * ACT_IRRIGATION_MAX, ACT_IRRIGATION_MAX,
           d[0] ? ", add NPK" : "", d[1] ? ", pH up" : "", d[2] ? ", pH down" : "");
  return String(buf);
}

// Runs the model on every new reading. Return "" to send no AI result.
String aiPredict(const Reading &r) {
  if (!(r.hasSoil && r.hasLight && r.hasAir)) return "";   // the model needs all three sensors
  float obs[GROWTHER_NUM_OBS];
  buildObservations(r, obs);

  float irr = 0;
  int d[GROWTHER_NUM_DISCRETE] = {0};
  unsigned long t0 = micros();
  growtherRun(obs, &irr, d);                               // your trained policy, deterministic actions
  unsigned long us = micros() - t0;
  Serial.printf("AI ran in %lu us: continuous %.3f, choices %d %d %d%s\n", us, irr, d[0], d[1], d[2],
                AI_INPUTS_COMPLETE ? "" : "   [TEST ONLY: inputs incomplete, not sent to the apps]");
  if (!AI_INPUTS_COMPLETE) return "";
  return describeActions(irr, d);
}
// ========================================================

void cloudLoop() {
  if (!CLOUD_ENABLED || WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lastMqttTry < 15000) return;
  lastMqttTry = millis();
  String cid = String("gw-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = MQTT_USER[0] ? mqtt.connect(cid.c_str(), MQTT_USER, MQTT_PASS) : mqtt.connect(cid.c_str());
  Serial.println(ok ? "Cloud connected" : "Cloud not reachable (local app still works)");
}

void handleNode() {
  String id = server.arg("id");  id.toUpperCase();
  String key = server.arg("key"); key.toUpperCase();
  server.sendHeader("Cache-Control", "no-store");
  if (id.length() != 6 || key != keyFor(id.c_str())) {
    server.send(403, "application/json", "{\"error\":\"bad key\"}");
    return;
  }
  for (int i = 0; i < nLatest; i++) {
    if (id == latest[i].id) {
      String j = latest[i].json;
      j.remove(j.length() - 1);                         // drop the closing }
      j += String(",\"age\":") + (millis() - latest[i].ms) / 1000 + "}";
      server.send(200, "application/json", j);
      return;
    }
  }
  server.send(200, "application/json", "{\"waiting\":true}");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  radioStart();
  if (!radioOk) { Serial.println("LoRa failed to start"); while (true) delay(1000); }
  radio.setPacketReceivedAction(onRx);
  radio.startReceive();

  WiFi.mode(WIFI_AP_STA);                // own WiFi + (optional) router WiFi at the same time
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("Phone WiFi QR text: WIFI:T:WPA;S:%s;P:%s;;\n", AP_SSID, AP_PASS);
  Serial.printf("Local app address: http://%s/\n", WiFi.softAPIP().toString().c_str());
  if (CLOUD_ENABLED && WIFI_SSID[0]) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
#if MQTT_TLS
  net.setInsecure();                     // skips certificate check (fine for a hobby project)
#endif
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(3);

  server.on("/", []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/node", handleNode);
  server.onNotFound([]() {                                  // pairing link from the QR code: /<id><key> (14 characters)
    if (server.uri().length() == 15) server.send_P(200, "text/html", INDEX_HTML);
    else server.send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("Gateway ready, waiting for packets");
}

void handlePacket() {
  Packet p;
  size_t len = radio.getPacketLength();
  if (len != sizeof(Packet) || radio.readData((uint8_t*)&p, sizeof(p)) != RADIOLIB_ERR_NONE) return;
  if (p.magic != 0xA5) return;
  uint8_t h[32];
  hmac256((uint8_t*)&p, sizeof(p) - 4, h);
  if (memcmp(h, p.tag, 4) != 0) { Serial.println("Bad signature, ignored"); return; }

  char id[7];
  snprintf(id, sizeof id, "%02X%02X%02X", p.id[0], p.id[1], p.id[2]);
  String j = String("{\"id\":\"") + id + "\",\"seq\":" + p.seq +
             ",\"rssi\":" + String(radio.getRSSI(), 0) + ",\"snr\":" + String(radio.getSNR(), 1);
  if (p.ok & 1) j += String(",\"moist\":") + String(p.soil[0] / 10.0, 1) +
                     ",\"soilT\":" + String((int16_t)p.soil[1] / 10.0, 1) +
                     ",\"ec\":" + p.soil[2] + ",\"ph\":" + String(p.soil[3] / PH_DIV, 2) +
                     ",\"n\":" + p.soil[4] + ",\"p\":" + p.soil[5] + ",\"k\":" + p.soil[6];
  if (p.ok & 2) j += String(",\"lux\":") + p.lux;
  if (p.ok & 4) j += String(",\"airT\":") + String(p.airT100 / 100.0, 1) +
                     ",\"airH\":" + String(p.airH100 / 100.0, 0) +
                     ",\"pres\":" + String(p.pres10 / 10.0, 1);
  // run your model on this reading and attach its answer
  Reading rd = {};
  if (p.ok & 1) { rd.hasSoil = true; rd.moist = p.soil[0] / 10.0; rd.soilT = (int16_t)p.soil[1] / 10.0; rd.ec = p.soil[2];
                  rd.ph = p.soil[3] / PH_DIV; rd.n = p.soil[4]; rd.p = p.soil[5]; rd.k = p.soil[6]; }
  if (p.ok & 2) { rd.hasLight = true; rd.lux = p.lux; }
  if (p.ok & 4) { rd.hasAir = true; rd.airT = p.airT100 / 100.0; rd.airH = p.airH100 / 100.0; rd.pres = p.pres10 / 10.0; }
  String ai = aiPredict(rd);
  ai.replace("\"", "'"); ai.replace("\\", "/"); ai.replace("\n", " ");   // keep the JSON valid
  if (ai.length()) j += String(",\"ai\":\"") + ai.substring(0, 60) + "\"";
  j += "}";
  Serial.println(j);

  // 1) keep for the local app
  int slot = -1;
  for (int i = 0; i < nLatest; i++) if (strcmp(latest[i].id, id) == 0) slot = i;
  if (slot < 0 && nLatest < 8) {
    slot = nLatest++;
    strcpy(latest[slot].id, id);
    Serial.printf("New node %s, pairing link: HTTP://192.168.4.1/%s%s\n", id, id, keyFor(id).c_str());
  }
  if (slot >= 0) { latest[slot].json = j; latest[slot].ms = millis(); }

  // 2) send to the cloud when it is reachable
  if (CLOUD_ENABLED && mqtt.connected()) {
    String topic = String("soilsys/") + id + "-" + keyFor(id) + "/data";
    mqtt.publish(topic.c_str(), j.c_str(), true);   // true = retained
  }
}

void loop() {
  server.handleClient();
  cloudLoop();
  if (rxFlag) {
    rxFlag = false;
    handlePacket();
    radio.startReceive();
  }
}
