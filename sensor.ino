// A.T.L.A.S. FIELD NODE (Heltec 2): reads the soil sensor (RS485), the BH1750 and the BME280,
// sends the values over LoRa to the brain, shows a boot screen, live readings and a pairing QR code on the OLED.
// Library to install: RadioLib only. Keep these files in this folder:
//   common.h  radio_setup.h  oled.h  qr_v2.h

#include <Wire.h>
#include "common.h"
#include "oled.h"

// ================= SETTINGS FOR THIS BOARD =================
#define SEND_INTERVAL_S    60
#define USE_DEEP_SLEEP     0     // 0 while testing, 1 for battery/solar use (screen only starts after a reset)
#define I2C_SDA   4              // BH1750 + BME280 share these two wires
#define I2C_SCL   5
#define RS485_RX  44              // board RX pin: connect it to the module's TXD pin (RO on bare chips)
#define RS485_TX  43              // board TX pin: connect it to the module's RXD pin (DI on bare chips)
#define RS485_DE  -1             // -1 = module switches direction itself; else the DE+RE pin
#define SOIL_BAUD 9600          // 4800 or 9600; the other one is tried automatically
#define SOIL_ADDR 1
// screen
#define SCREEN_ENABLED     1
#define QR_SHOW_SECONDS    30    // the pairing QR stays on this long after power-on
#define SCREEN_OFF_AFTER_S 120   // screen sleeps after this many seconds without a button press (0 = never)
#define PAGE_SECONDS       4     // how long each readings page is shown
#define BUTTON_PIN         0     // the PRG button: press it to show the QR code again
// ===========================================================

RTC_DATA_ATTR uint16_t seq = 0;
uint8_t nodeId[3];
char idStr[7];
static unsigned long soilBaud = SOIL_BAUD;

// ---------------------------------------------------------------- helpers
static uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

static bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool i2cWrite(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool i2cCmd(uint8_t addr, uint8_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write(cmd);
  return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------- BH1750 (light)
static bool readLight(float& lux) {
  const uint8_t addrs[] = {0x23, 0x5C};
  uint8_t addr = 0;
  for (uint8_t a : addrs) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { addr = a; break; }
  }
  if (!addr) return false;
  if (!i2cCmd(addr, 0x01) || !i2cCmd(addr, 0x20)) return false;   // power on, one high-resolution measurement
  delay(180);
  if (Wire.requestFrom((int)addr, 2) != 2) return false;
  uint16_t raw = Wire.read() << 8;
  raw |= Wire.read();
  lux = raw / 1.2f;
  return true;
}

// ---------------------------------------------------------------- BME280 / BMP280 (air)
struct AirData { float t, h, p; bool hasH; };

static bool readAir(AirData& o) {
  const uint8_t addrs[] = {0x76, 0x77};
  uint8_t addr = 0, id = 0;
  for (uint8_t a : addrs) {
    if (i2cRead(a, 0xD0, &id, 1) && (id == 0x60 || id == 0x58 || id == 0x56 || id == 0x57)) { addr = a; break; }
  }
  if (!addr) return false;
  o.hasH = (id == 0x60);   // 0x60 = BME280, 0x56-0x58 = BMP280 (no humidity)

  uint8_t c1[26], c2[7] = {0};
  if (!i2cRead(addr, 0x88, c1, 26)) return false;
  uint16_t T1 = c1[0] | (c1[1] << 8);
  int16_t T2 = c1[2] | (c1[3] << 8), T3 = c1[4] | (c1[5] << 8);
  uint16_t P1 = c1[6] | (c1[7] << 8);
  int16_t P2 = c1[8] | (c1[9] << 8), P3 = c1[10] | (c1[11] << 8), P4 = c1[12] | (c1[13] << 8);
  int16_t P5 = c1[14] | (c1[15] << 8), P6 = c1[16] | (c1[17] << 8), P7 = c1[18] | (c1[19] << 8);
  int16_t P8 = c1[20] | (c1[21] << 8), P9 = c1[22] | (c1[23] << 8);
  uint8_t H1 = c1[25], H3 = 0;
  int16_t H2 = 0, H4 = 0, H5 = 0;
  int8_t H6 = 0;
  if (o.hasH) {
    if (!i2cRead(addr, 0xE1, c2, 7)) return false;
    H2 = c2[0] | (c2[1] << 8);
    H3 = c2[2];
    H4 = ((int8_t)c2[3] * 16) + (c2[4] & 0x0F);
    H5 = ((int8_t)c2[5] * 16) + (c2[4] >> 4);
    H6 = (int8_t)c2[6];
    if (!i2cWrite(addr, 0xF2, 0x01)) return false;   // humidity x1
  }
  if (!i2cWrite(addr, 0xF4, 0x25)) return false;     // temperature x1, pressure x1, single measurement
  delay(20);
  uint8_t d[8] = {0};
  if (!i2cRead(addr, 0xF7, d, o.hasH ? 8 : 6)) return false;

  int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
  int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
  int32_t adcH = ((int32_t)d[6] << 8) | d[7];

  int32_t v1 = ((((adcT >> 3) - ((int32_t)T1 << 1))) * (int32_t)T2) >> 11;
  int32_t v2 = (((((adcT >> 4) - (int32_t)T1) * ((adcT >> 4) - (int32_t)T1)) >> 12) * (int32_t)T3) >> 14;
  int32_t tFine = v1 + v2;
  o.t = ((tFine * 5 + 128) >> 8) / 100.0f;

  int64_t p1 = (int64_t)tFine - 128000;
  int64_t p2 = p1 * p1 * (int64_t)P6;
  p2 += (p1 * (int64_t)P5) << 17;
  p2 += ((int64_t)P4) << 35;
  p1 = ((p1 * p1 * (int64_t)P3) >> 8) + ((p1 * (int64_t)P2) << 12);
  p1 = (((((int64_t)1) << 47) + p1)) * (int64_t)P1 >> 33;
  if (p1 == 0) return false;
  int64_t pr = 1048576 - adcP;
  pr = (((pr << 31) - p2) * 3125) / p1;
  p1 = ((int64_t)P9 * (pr >> 13) * (pr >> 13)) >> 25;
  p2 = ((int64_t)P8 * pr) >> 19;
  pr = ((pr + p1 + p2) >> 8) + ((int64_t)P7 << 4);
  o.p = (pr / 256.0f) / 100.0f;   // Pa -> hPa

  o.h = 0;
  if (o.hasH) {
    int32_t x = tFine - 76800;
    x = (((((adcH << 14) - ((int32_t)H4 << 20) - ((int32_t)H5 * x)) + 16384) >> 15) *
         (((((((x * (int32_t)H6) >> 10) * (((x * (int32_t)H3) >> 11) + 32768)) >> 10) + 2097152) * (int32_t)H2 + 8192) >> 14));
    x = x - (((((x >> 15) * (x >> 15)) >> 7) * (int32_t)H1) >> 4);
    x = x < 0 ? 0 : x;
    x = x > 419430400 ? 419430400 : x;
    o.h = (x >> 12) / 1024.0f;
  }
  return true;
}

// ---------------------------------------------------------------- soil sensor (RS485 Modbus)
static bool pollSoil(uint16_t* out) {
  uint8_t req[8] = {SOIL_ADDR, 0x03, 0x00, 0x00, 0x00, 0x07, 0, 0};
  uint16_t c = crc16(req, 6);
  req[6] = c & 0xFF;
  req[7] = c >> 8;
  while (Serial1.available()) Serial1.read();
  if (RS485_DE >= 0) digitalWrite(RS485_DE, HIGH);
  Serial1.write(req, 8);
  Serial1.flush();
  if (RS485_DE >= 0) digitalWrite(RS485_DE, LOW);
  uint8_t buf[19];
  if (Serial1.readBytes(buf, 19) != 19) return false;
  if (buf[0] != SOIL_ADDR || buf[1] != 0x03 || buf[2] != 14) return false;
  if (crc16(buf, 17) != (uint16_t)(buf[17] | (buf[18] << 8))) return false;
  for (int i = 0; i < 7; i++) out[i] = (buf[3 + 2 * i] << 8) | buf[4 + 2 * i];
  return true;
}

static bool readSoil(uint16_t* out) {
  for (int attempt = 0; attempt < 2; attempt++) {
    if (pollSoil(out)) return true;
    soilBaud = (soilBaud == 4800) ? 9600 : 4800;   // no answer: try the other common baud rate
    Serial1.updateBaudRate(soilBaud);
    delay(50);
  }
  return false;
}

// ---------------------------------------------------------------- latest readings (for the screen)
struct Latest {
  bool soil = false, light = false, air = false;
  uint16_t s[7] = {0};
  float lux = 0;
  AirData a = {0, 0, 0, false};
  uint16_t seq = 0;
  int sent = -1;                 // -1 nothing sent yet, 1 sent, 0 failed
} last;

static char qrText[48];
static QrCode qr;
static bool qrReady = false;
static bool screenOk = false, screenOn = false, cold = true;
static int mode = 0;                         // 0 = readings pages, 1 = pairing QR
static unsigned long qrUntil = 0, lastTouch = 0, lastDraw = 0, nextSend = 0, awakeUntil = 0;
static bool sentOnce = false;

// ---------------------------------------------------------------- screens
static void rowText(int y, const char *label, const char *value) {
  oledText(1, y, label);
  oledText(127 - oledTextWidth(value), y, value);
}

static void drawSplash() {
  oledClear();
  oledCenter(8, "A.T.L.A.S.", 2);
  oledCenter(30, "FIELD NODE");
  oledCenter(40, idStr);
  oledShow();
  for (int i = 0; i <= 100; i += 10) {                    // short progress bar
    oledFrame(14, 52, 100, 8);
    oledFill(16, 54, i * 96 / 100, 4);
    oledShow();
    delay(100);
  }
}

static const char *stText(int s) { return s < 0 ? "..." : (s ? "OK" : "FAIL"); }

static void drawSelfTest(const int *st) {
  oledClear();
  oledText(1, 0, "SELF TEST");
  oledFill(0, 9, 128, 1);
  rowText(14, "LORA RADIO", stText(st[0]));
  rowText(26, "SOIL SENSOR", stText(st[1]));
  rowText(38, "LIGHT SENSOR", stText(st[2]));
  rowText(50, "AIR SENSOR", stText(st[3]));
  oledShow();
}

static void drawQrScreen() {
  oledClear();
  if (!qrReady) { oledCenter(28, "NO QR"); oledShow(); return; }
  oledQR(qr, 64, 0, 2, 7);                                // 25 modules x 2 px + 7 px border = 64 px square
  oledText(2, 4, "SCAN TO");
  oledText(2, 14, "PAIR");
  oledText(2, 30, "ID");
  oledText(2, 40, idStr);
  oledText(2, 54, "PRG=BACK");
  oledShow();
}

static void drawFooter() {
  char b[24];
  oledFill(0, 54, 128, 1);
  if (last.sent < 0) snprintf(b, sizeof b, "WAITING");
  else snprintf(b, sizeof b, "TX %u %s", last.seq, last.sent ? "OK" : "FAIL");
  oledText(1, 57, b);
  oledText(127 - oledTextWidth(idStr), 57, idStr);
}

static void drawPage(int page) {
  char v[24];
  oledClear();
  if (page == 0) {
    oledText(1, 0, "SOIL");
    if (last.soil) {
      snprintf(v, sizeof v, "%.1f %%", last.s[0] / 10.0f);  rowText(12, "MOISTURE", v);
      snprintf(v, sizeof v, "%.1f C", (int16_t)last.s[1] / 10.0f); rowText(22, "TEMP", v);
      snprintf(v, sizeof v, "%.1f", last.s[3] / 10.0f);     rowText(32, "PH", v);
      snprintf(v, sizeof v, "%u US/CM", last.s[2]);         rowText(42, "EC", v);
    } else oledCenter(28, "NO SOIL DATA");
  } else if (page == 1) {
    oledText(1, 0, "NUTRIENTS");
    if (last.soil) {
      snprintf(v, sizeof v, "%u MG/KG", last.s[4]);  rowText(12, "NITROGEN", v);
      snprintf(v, sizeof v, "%u MG/KG", last.s[5]);  rowText(22, "PHOSPHORUS", v);
      snprintf(v, sizeof v, "%u MG/KG", last.s[6]);  rowText(32, "POTASSIUM", v);
    } else oledCenter(28, "NO SOIL DATA");
  } else {
    oledText(1, 0, "AIR + LIGHT");
    if (last.air) {
      snprintf(v, sizeof v, "%.1f C", last.a.t);     rowText(12, "TEMP", v);
      if (last.a.hasH) { snprintf(v, sizeof v, "%.0f %%", last.a.h); rowText(22, "HUMIDITY", v); }
      snprintf(v, sizeof v, "%.0f HPA", last.a.p);   rowText(32, "PRESSURE", v);
    } else rowText(12, "AIR", "--");
    if (last.light) { snprintf(v, sizeof v, "%.0f LX", last.lux); rowText(42, "LIGHT", v); }
    else rowText(42, "LIGHT", "--");
  }
  oledFill(0, 9, 128, 1);
  drawFooter();
  oledShow();
}

static void touchScreen() {
  lastTouch = millis();
  if (screenOk && !screenOn) { oledWake(); screenOn = true; }
}

static bool buttonPressed() {
  static int prev = HIGH;
  static unsigned long lastEdge = 0;
  int now = digitalRead(BUTTON_PIN);
  bool hit = (prev == HIGH && now == LOW && millis() - lastEdge > 250);
  if (prev != now) lastEdge = millis();
  prev = now;
  return hit;
}

// ---------------------------------------------------------------- measure and send
static void measureAndSend() {
  Packet p = {};
  p.magic = 0xA5;
  memcpy(p.id, nodeId, 3);
  p.seq = seq++;

  last.soil = last.light = last.air = false;
  uint16_t soil[7];                                   // aligned copy: the packet is packed, so write into it with memcpy
  if (readSoil(soil)) { memcpy(p.soil, soil, sizeof soil); memcpy(last.s, soil, sizeof soil); last.soil = true; p.ok |= 1; }
  float lux;
  if (readLight(lux)) { last.light = true; last.lux = lux; p.lux = lux > 65535 ? 65535 : (uint16_t)lux; p.ok |= 2; }
  AirData a;
  if (readAir(a)) {
    last.air = true; last.a = a;
    p.airT100 = (int16_t)(a.t * 100);
    p.airH100 = (uint16_t)(a.h * 100);
    p.pres10  = (uint16_t)(a.p * 10);
    p.ok |= 4;
  }
  Serial.printf("seq %u ok=%u (1 soil, 2 light, 4 air; 7 = all) soil: %u %u %u %u %u %u %u | lux %u | air %.2fC\n", p.seq, p.ok,
                p.soil[0], p.soil[1], p.soil[2], p.soil[3], p.soil[4], p.soil[5], p.soil[6], p.lux, p.airT100 / 100.0);

  uint8_t h[32];
  hmac256((uint8_t*)&p, sizeof(p) - 4, h);
  memcpy(p.tag, h, 4);

  int s = radio.transmit((uint8_t*)&p, sizeof(p));
  last.seq = p.seq;
  last.sent = (s == RADIOLIB_ERR_NONE) ? 1 : 0;
  Serial.println(last.sent ? "sent" : "send FAILED");
  radio.sleep();
  sentOnce = true;
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  delay(300);
  cold = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED);   // false when waking from deep sleep
  uint64_t mac = ESP.getEfuseMac();
  nodeId[0] = mac >> 24; nodeId[1] = mac >> 32; nodeId[2] = mac >> 40;
  snprintf(idStr, sizeof idStr, "%02X%02X%02X", nodeId[0], nodeId[1], nodeId[2]);
  snprintf(qrText, sizeof qrText, "HTTP://192.168.4.1/%s%s", idStr, keyFor(idStr).c_str());
  qrReady = qrMake(qrText, &qr);
  Serial.printf("\nNODE ID: %s\n", idStr);
  Serial.printf("QR TEXT: %s\n", qrText);

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial1.begin(soilBaud, SERIAL_8N1, RS485_RX, RS485_TX);
  Serial1.setTimeout(600);
  if (RS485_DE >= 0) { pinMode(RS485_DE, OUTPUT); digitalWrite(RS485_DE, LOW); }
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (SCREEN_ENABLED && cold) { screenOk = oledBegin(); screenOn = screenOk; if (!screenOk) Serial.println("OLED not found"); }
  if (screenOk) drawSplash();

  // power-on self test, shown on the screen
  int st[4] = {-1, -1, -1, -1};
  if (screenOk) drawSelfTest(st);
  radioStart();
  st[0] = radioOk ? 1 : 0;
  if (screenOk) drawSelfTest(st);
  uint16_t s7[7]; float lx; AirData ad;
  st[1] = readSoil(s7) ? 1 : 0;   if (screenOk) drawSelfTest(st);
  st[2] = readLight(lx) ? 1 : 0;  if (screenOk) drawSelfTest(st);
  st[3] = readAir(ad) ? 1 : 0;    if (screenOk) drawSelfTest(st);
  Serial.printf("self test: radio %s, soil %s, light %s, air %s\n", stText(st[0]), stText(st[1]), stText(st[2]), stText(st[3]));

  if (!radioOk) {                                       // nothing to do without a radio: show it, then try again
    if (screenOk) { oledCenter(2, "RADIO FAILED"); oledShow(); }
    delay(8000);
    ESP.restart();
  }
  if (screenOk) { delay(2000); mode = 1; qrUntil = millis() + QR_SHOW_SECONDS * 1000UL; lastTouch = millis(); }
  awakeUntil = (cold && screenOk) ? qrUntil + 2000UL : 0;
  nextSend = millis();
}

void loop() {
  unsigned long now = millis();

  if (buttonPressed()) {
    bool wasOff = !screenOn;
    touchScreen();
    if (screenOk) { mode = (wasOff || mode == 0) ? 1 : 0; qrUntil = now + QR_SHOW_SECONDS * 1000UL; if (USE_DEEP_SLEEP) awakeUntil = qrUntil; }
  }
  if (screenOk && screenOn) {
    if (SCREEN_OFF_AFTER_S && mode == 0 && now - lastTouch > SCREEN_OFF_AFTER_S * 1000UL) { oledOff(); screenOn = false; }
    if (mode == 1 && now > qrUntil) { mode = 0; lastTouch = now; }
    if (screenOn && now - lastDraw >= 250) {
      lastDraw = now;
      if (mode == 1) drawQrScreen(); else drawPage((now / (PAGE_SECONDS * 1000UL)) % 3);
    }
  }

  if ((long)(now - nextSend) >= 0) {
    measureAndSend();
    nextSend = millis() + SEND_INTERVAL_S * 1000UL;
  }

#if USE_DEEP_SLEEP
  if (sentOnce && millis() >= awakeUntil) {
    if (screenOk) oledOff();
    esp_sleep_enable_timer_wakeup((uint64_t)SEND_INTERVAL_S * 1000000ULL);
    esp_deep_sleep_start();
  }
#endif
  delay(20);
}
