// Radio start-up for the Heltec V4.
// IF YOU ALREADY HAVE A RADIO SETUP THAT WORKS ON YOUR BOARDS (the one that printed
// "LoRa ready ... FEM type 2"), paste it into radioStart() instead of mine.
// Keep the name `radio` for the SX1262 object, set radioOk to true when it worked,
// and use the same frequency and settings on BOTH boards.
#pragma once

SX1262 radio = new Module(8, 14, 12, 13);   // NSS, DIO1, RESET, BUSY (same as the Heltec V3)
bool radioOk = false;                       // radioStart() sets this

void radioStart() {
  pinMode(7, OUTPUT);  digitalWrite(7, HIGH);   // V4 amplifier power   (pins can differ between V4 revisions)
  pinMode(2, OUTPUT);  digitalWrite(2, HIGH);   // V4 amplifier enable
  int s = radio.begin(LORA_FREQ, 125.0, 9, 7, 0x12, LORA_POWER, 8);
  radio.setDio2AsRfSwitch(true);
  radioOk = (s == RADIOLIB_ERR_NONE);
  if (!radioOk) { Serial.printf("LoRa init failed, code %d\n", s); return; }
  Serial.printf("LoRa ready at %.1f MHz, drive %d dBm\n", LORA_FREQ, LORA_POWER);
}
