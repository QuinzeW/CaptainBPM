// CaptainBPM_v1_FINAL.ino
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define BPM_UUID            "12345678-1234-1234-1234-1234567890ac"
#define BEAT_UUID           "12345678-1234-1234-1234-1234567890ad"
#define START_UUID          "12345678-1234-1234-1234-1234567890ae"
#define CLIENTS_UUID        "12345678-1234-1234-1234-1234567890b0"

#define BTN_UP 21
#define BTN_DOWN 23
#define NEO_PIN 8
#define NEO_COUNT 1

Adafruit_NeoPixel pixels(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

NimBLECharacteristic *bpmChar;
NimBLECharacteristic *beatChar; 
NimBLECharacteristic *startChar;
NimBLECharacteristic *clientsChar;
NimBLEServer *pServer;

int clientCount = 0;  // SIMPLIFIÉ: juste le nombre (MAC optionnel après)

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

// Déclarations AVANT
void sendBPM();
void notifyBeat();
void notifyStartTime();
void broadcastClients();
void pulseOn();
void startSession();

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    clientCount++;
    Serial.printf("Client connecté #%d\n", clientCount);
    broadcastClients();
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    if (clientCount > 0) clientCount--;
    Serial.printf("Client déconnecté, reste %d\n", clientCount);
    broadcastClients();
    NimBLEDevice::startAdvertising();
  }
};

void broadcastClients() {
  if (!clientsChar) return;
  
  String json = String("{\"clients\":") + clientCount + "}";
  clientsChar->setValue((uint8_t*)json.c_str(), json.length());
  clientsChar->notify();
  Serial.printf("Broadcast clients: %d\n", clientCount);
}

void sendBPM() {
  if (bpmChar) {
    bpmChar->setValue(String(bpm).c_str());
    bpmChar->notify();
  }
}

void notifyBeat() {
  if (beatChar) {
    beatChar->setValue(String(beatCount).c_str());
    beatChar->notify();
  }
}

void notifyStartTime() {
  if (startChar) {
    startChar->setValue(String(sessionStartMs).c_str());
    startChar->notify();
  }
}

void pulseOn() {
  pixels.setPixelColor(0, pixels.Color(12, 10, 14));
  pixels.show();
  pulseActive = true;
  pulseOffAtMs = millis() + pulseDurationMs;
  beatCount++;
  notifyBeat();
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
  notifyStartTime();
  sendBPM();
  notifyBeat();
  Serial.println("Session démarrée");
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
  NimBLEDevice::init("CaptainBPM");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = pServer->createService(SERVICE_UUID);

  bpmChar = service->createCharacteristic(
    BPM_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  bpmChar->setValue("120");

  beatChar = service->createCharacteristic(
    BEAT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  startChar = service->createCharacteristic(
    START_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  clientsChar = service->createCharacteristic(
    CLIENTS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  service->start();

  pServer->setMaxConnections(6);  // CORRIGÉ: Multi-clients
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  // CORRIGÉ: Pas de scanResponseData complexe
  NimBLEDevice::startAdvertising();
  Serial.println("BLE advertising démarré");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Démarrage CaptainBPM v1");
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
    notifyStartTime();
    Serial.println("Timecode refresh");
  }

  delay(5);
}