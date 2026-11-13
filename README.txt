A.T.L.A.S.: brain + field node + websites

FOLDERS
  brain/            Heltec 1 (the brain). Open brain/brain.ino in Arduino IDE and upload.
  sensor/           Heltec 2 (field node with the 3 sensors). Open sensor/sensor.ino and upload.
  cloud-website/    index.html: optional viewer from anywhere (host it on Netlify Drop).
  The OFFLINE website is inside the brain (webpage.h). Join its WiFi and open http://192.168.4.1

LIBRARIES (Arduino IDE > Library Manager)
  RadioLib                      both boards
  PubSubClient (Nick O'Leary)   brain only
  (the screen and the QR code need no library)

BEFORE YOU UPLOAD
  1. common.h exists in BOTH folders and must be identical: same MASTER_SECRET, same LORA_FREQ.
  2. brain/brain.ino: fill in WIFI_SSID / WIFI_PASS (your router, for the cloud) and the MQTT broker.
  3. radio_setup.h: if you already have a radio setup that works, paste it into radioStart()
     (set radioOk = true when it works).
  4. Board settings: your Heltec V4 board, USB CDC On Boot = Enabled, Serial Monitor at 115200.

WHAT THE FIELD NODE'S SCREEN DOES
  power on  -> A.T.L.A.S. splash -> self test (radio, soil, light, air) -> pairing QR code for 30 s
            -> readings pages (soil, nutrients, air + light) that change every 4 s
  PRG button -> shows the QR code again (press again to go back)
  the screen sleeps after 120 s without a button press; the PRG button wakes it

PAIRING
  1. Join the brain's WiFi:  SoilGateway  (password in brain.ino, AP_PASS)
  2. Scan the QR code on the field node's screen with your phone camera.
  3. The offline app opens and the system is added. Repeat for every field node.
  The same link is also printed on the node's Serial Monitor ("QR TEXT: ...") if you want a printed sticker.

THE AI
  growther_model.h is your trained model. Its result stays hidden from the apps until
  AI_INPUTS_COMPLETE is set to 1 in brain.ino (needs all 75 model inputs filled in).
