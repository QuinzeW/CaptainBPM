#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

// MIDI BLE OFFICIEL
#define MIDI_SERVICE_UUID "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_IO_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

#define BTN_UP 23
#define BTN_DOWN 21
#define NEO_PIN 8
#define NEO_COUNT 1

Adafruit_NeoPixel pixels(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
NimBLECharacteristic *midiChar;
NimBLEServer *pServer;

int clientCount = 0;
int bpm = 120;
uint32_t beatCount = 0;
uint32_t sessionStartMs = 0;
bool running = false;
uint32_t lastStepMs = 0;
uint32_t pulseOffAtMs = 0;
bool pulseActive = false;
uint32_t pulseDurationMs = 10;
uint32_t lastTimecodeSend = 0;

bool lastBtnUp = HIGH, lastBtnDown = HIGH;
uint32_t lastBtnUpMs = 0, lastBtnDownMs = 0;
const uint32_t debounceMs = 40;

// DÉCLARATIONS AVANT les classes
void sendBPM();
void sendBeat();
void sendTimecode();
void sendClients();
void sendAllData();
void pulseOn();
void startSession();

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    clientCount++;
    Serial.printf("Client MIDI #%d\n", clientCount);
    sendAllData();
    NimBLEDevice::startAdvertising();  // Multi-clients
  }
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    if (clientCount > 0) clientCount--;
    Serial.printf("Client déconnecté: %d restants\n", clientCount);
    sendAllData();
    NimBLEDevice::startAdvertising();
  }
};

class MidiCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {  // ← 2 params !
    std::string RxValue = pChar->getValue();
    if (RxValue.length() >= 2) {
      uint8_t type = RxValue[0];
      uint8_t newBpm = RxValue[1];
      if (type == 0x01 && newBpm >= 20 && newBpm <= 300) {
        bpm = newBpm;
        Serial.printf("BPM reçu client %s: %d\n", connInfo.getAddress().toString().c_str(), bpm);
        sendBPM();
        if (running) startSession();
      }
    }
  }
};

// Envoi MIDI-like: [Type][BPM/Beat/Timecode][Valeur]
void sendBPM() {
  if (!midiChar) return;
  uint8_t packet[4] = {0x01, (uint8_t)bpm, 0, 0};  // Type 1 = BPM
  midiChar->setValue(packet, 4);
  midiChar->notify();
}

void sendBeat() {
  if (!midiChar) return;
  uint8_t packet[5] = {0x02, beatCount & 0xFF, (beatCount >> 8) & 0xFF, 0, 0};  // Type 2 = Beat
  midiChar->setValue(packet, 5);
  midiChar->notify();
}

void sendTimecode() {
  if (!midiChar) return;
  uint8_t packet[7] = {0x03, sessionStartMs & 0xFF, (sessionStartMs >> 8) & 0xFF,
                       (sessionStartMs >> 16) & 0xFF, (sessionStartMs >> 24) & 0xFF, 0, 0};  // Type 3
  midiChar->setValue(packet, 7);
  midiChar->notify();
}

void sendClients() {
  if (!midiChar) return;
  uint8_t packet[2] = {0x04, (uint8_t)clientCount};  // Type 4 = Clients
  midiChar->setValue(packet, 2);
  midiChar->notify();
}

void sendAllData() {
  sendClients();
  sendBPM();
  if (running) {
    sendTimecode();
    sendBeat();
  }
}

void pulseOn() {
  pixels.setPixelColor(0, pixels.Color(12, 10, 14));
  pixels.show();
  pulseActive = true;
  pulseOffAtMs = millis() + pulseDurationMs;
  beatCount++;
  sendBeat();
}

void updatePulse(uint32_t now) {
  if (!pulseActive) return;
  if ((int32_t)(now - pulseOffAtMs) >= 0) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    pulseActive = false;
  }
}

bool pressedEdge(uint8_t pin, bool& lastState, uint32_t& lastChange, uint32_t now) {
  bool reading = !digitalRead(pin);
  if (reading != lastState && now - lastChange > debounceMs) {
    lastChange = now;
    lastState = reading;
    return reading;
  }
  return false;
}

void startSession() {
  running = true;
  beatCount = 0;
  sessionStartMs = millis();
  lastStepMs = millis();
  sendAllData();
  Serial.println("Session MIDI démarrée");
}

void updateMetronome(uint32_t now) {
  if (!running) return;
  uint32_t interval = 60000UL / bpm;
  if (now - lastStepMs >= interval) {
    lastStepMs += interval;
    pulseOn();
  }
}

void setupButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
}

void setupBLE() {
  NimBLEDevice::init("CaptainBPM-MIDI");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = pServer->createService(MIDI_SERVICE_UUID);

  midiChar = service->createCharacteristic(
    MIDI_IO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    midiChar->setValue("120");  // BPM initial pour read
  
  midiChar->setCallbacks(new MidiCallbacks());
  midiChar->setValue(String(bpm).c_str());

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(MIDI_SERVICE_UUID);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("MIDI BLE prêt");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("CaptainBPM MIDI v2");
  pixels.begin();
  pixels.clear();
  pixels.show();
  setupButtons();
  setupBLE();
  startSession();
}

void loop() {
  uint32_t now = millis();

  if (pressedEdge(BTN_UP, lastBtnUp, lastBtnUpMs, now)) {
    bpm = min(300, bpm + 1);
    sendBPM();
    if (running) startSession();
    Serial.printf("BPM+: %d\n", bpm);
  }
  if (pressedEdge(BTN_DOWN, lastBtnDown, lastBtnDownMs, now)) {
    bpm = max(20, bpm - 1);
    sendBPM();
    if (running) startSession();
    Serial.printf("BPM-: %d\n", bpm);
  }

  updateMetronome(now);
  updatePulse(now);

  if (running && now - lastTimecodeSend > 10000) {
    lastTimecodeSend = now;
    sendTimecode();
  }

  delay(5);
}