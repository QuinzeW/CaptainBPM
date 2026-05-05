#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

#define SERVICE_UUID   "12345678-1234-1234-1234-1234567890ab"
#define BPM_CHAR_UUID  "12345678-1234-1234-1234-1234567890ac"
#define BEAT_CHAR_UUID  "12345678-1234-1234-1234-1234567890ad"
//#define START_CHAR_UUID   "12345678-1234-1234-1234-1234567890ae"

#define NIMBLE_MAX_CONNECTIONS 6 

#define BTN_UP   21
#define BTN_DOWN 23
#define NEOPIXEL_PIN  8
#define NEOPIXEL_COUNT 1

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

NimBLECharacteristic* bpmChar = nullptr;
NimBLECharacteristic* beatChar = nullptr;
// NimBLECharacteristic* startChar = nullptr;
NimBLEServer* pServer = nullptr;

volatile bool deviceConnected = false;

int bpm = 120;
uint32_t beatCount = 0;
uint32_t startTimeMs = 0;
bool running = false;

uint32_t lastStepMs = 0;
uint32_t pulseOffAtMs = 0;
bool pulseActive = false;
uint32_t pulseDurationMs = 10;

bool lastBtnUpState = HIGH;
bool lastBtnDownState = HIGH;
uint32_t lastBtnUpChangeMs = 0;
uint32_t lastBtnDownChangeMs = 0;
const uint32_t debounceMs = 40;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    // deviceConnected = true;
    Serial.println("Client connected");
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    // deviceConnected = false;
    Serial.printf("Client disconnected, reason=%d\n", reason);
    delay(100);
    NimBLEDevice::startAdvertising();
    Serial.println("Advertising restarted after disconnect");
  }
};

void sendBPM() {
  String s = String(bpm);
  bpmChar->setValue(s.c_str());
  bpmChar->notify();
  Serial.printf("BPM=%d\n", bpm);
}

void notifyBeat() {
  String s = String(beatCount);
  beatChar->setValue(s.c_str());
  beatChar->notify();
}

void notifyStartTime() {
  String s = String(startTimeMs);
  startChar->setValue(s.c_str());
  startChar->notify();
  Serial.printf("START_TIME=%lu\n", startTimeMs);
}

void pulseOn() {
  pixels.setPixelColor(0, pixels.Color(12, 10, 14));
  pixels.show();
  delay (1);
  pulseActive = true;
  pulseOffAtMs = millis() + pulseDurationMs;
  beatCount++;
  notifyBeat();
}

void updatePulse(uint32_t now) {
  if (pulseActive && (int32_t)(now - pulseOffAtMs) >= 0) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    pixels.show();
    delay (1);
    pulseActive = false;
  }
}

void updateMetronome(uint32_t now) {
  // if (!running) return;
  uint32_t intervalMs = 60000UL / bpm;
  if ((uint32_t)(now - lastStepMs) >= intervalMs) {
    lastStepMs += intervalMs;
    pulseOn();
  }
}

bool pressedEdge(uint8_t pin, bool &lastState, uint32_t &lastChangeMs, uint32_t now) {
  bool reading = digitalRead(pin);
   if (reading != lastState && (now - lastChangeMs) > debounceMs) {
    lastChangeMs = now;
    lastState = reading;
    return reading == LOW;
  }
  return false;
}

void startSession() {
  running = true;
  beatCount = 0;
  startTimeMs = millis() + 2000;
  notifyStartTime();
  lastStepMs = startTimeMs - (60000 / bpm);
  Serial.println("Session started");
}

void setupButtons() {
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
}

void setupBle() {
  NimBLEDevice::init("CaptainBPM");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* service = pServer->createService(SERVICE_UUID);

  bpmChar = service->createCharacteristic(
    BPM_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  bpmChar->setValue(String(bpm).c_str());
  bpmChar->addDescriptor(new NimBLE2904());

  beatChar = service->createCharacteristic(
    BEAT_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  beatChar->setValue("0");
  beatChar->addDescriptor(new NimBLE2904());

  // startChar = service->createCharacteristic(
  //   START_CHAR_UUID,
  //   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  // );
  // startChar->setValue("0");
  // startChar->addDescriptor(new NimBLE2904());

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  // adv->setScanResponse(true);
  adv->start();

  Serial.println("BLE advertising started");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  setupButtons();

  pixels.begin();
  pixels.clear();
  pixels.show();

  setupBle();
  // startSession();
}

void loop() {
  uint32_t now = millis();

  if (pressedEdge(BTN_UP, lastBtnUpState, lastBtnUpChangeMs, now)) {
    bpm++;
    if (bpm > 300) bpm = 300;
    sendBPM();

  if (running) {
    startTimeMs = now + 2000;
    notifyStartTime();
    lastStepMs = startTimeMs - (60000 / bpm);
    }
  }

  if (pressedEdge(BTN_DOWN, lastBtnDownState, lastBtnDownChangeMs, now)) {
    bpm--;
    if (bpm < 20) bpm = 20;
    sendBPM();

  if (running) {
    startTimeMs = now + 2000;
    notifyStartTime();
    lastStepMs = startTimeMs - (60000 / bpm);
    }
  }

  updateMetronome(now);
  updatePulse(now);

  delay(1);
}