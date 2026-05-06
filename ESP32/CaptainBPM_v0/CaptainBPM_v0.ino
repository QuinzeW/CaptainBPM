#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>

#define SERVICE_UUID   "12345678-1234-1234-1234-1234567890ab"
#define BPM_CHAR_UUID  "12345678-1234-1234-1234-1234567890ac"
#define BEAT_CHAR_UUID  "12345678-1234-1234-1234-1234567890ad"
#define START_CHAR_UUID   "12345678-1234-1234-1234-1234567890ae"
#define POSITION_CHAR_UUID "12345678-1234-1234-1234-1234567890af"

#define NIMBLE_MAX_CONNECTIONS 6 

#define BTN_UP   21
#define BTN_DOWN 23
#define NEOPIXEL_PIN  8
#define NEOPIXEL_COUNT 1

Adafruit_NeoPixel pixels(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Caractéristiques BLE globales
NimBLECharacteristic* bpmChar = nullptr;
NimBLECharacteristic* beatChar = nullptr;
NimBLECharacteristic* startChar = nullptr;
NimBLECharacteristic* positionChar = nullptr;  // globale
NimBLEServer* pServer = nullptr;

volatile bool deviceConnected = false;

// État du métronome
int      bpm           = 120;
uint32_t beatCount     = 0;
uint32_t sessionStartMs = 0;    // horloge absolue de début de session
bool     running       = false;
uint32_t lastStepMs    = 0;

// Gestion du flash LED
uint32_t pulseOffAtMs    = 0;
bool     pulseActive     = false;
uint32_t pulseDurationMs = 10;

// Anti-rebond boutons
bool     lastBtnUpState     = HIGH;
bool     lastBtnDownState   = HIGH;
uint32_t lastBtnUpChangeMs  = 0;
uint32_t lastBtnDownChangeMs= 0;
const uint32_t debounceMs   = 40;

// Callbacks serveur BLE
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println("Client connected");
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.printf("Client disconnected, reason=%d\n", reason);
    delay(500);
    NimBLEDevice::startAdvertising();
    Serial.println("Advertising restarted after disconnect");
  }
};

// Envoi des caractéristiques BLE
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
    // On envoie l'instant de début de la session en millisecondes (uint32)
    // comme un texte, pour rester compatible avec le code web actuel.
    String s = String(sessionStartMs);
    startChar->setValue(s.c_str());
    startChar->notify();
    Serial.printf("SESSION_START=%lu\n", sessionStartMs);
}

// LED
void pulseOn() {
  pixels.setPixelColor(0, pixels.Color(12, 10, 14));
  pixels.show();
  delay (1);
  pulseActive = true;
  pulseOffAtMs = millis() + pulseDurationMs;

  // On avance le beatCount et on notifie tous les clients
  beatCount++;
  notifyBeat();
}

void updatePulse(uint32_t now) {
    if (!pulseActive) return;
    if ((int32_t)(now - pulseOffAtMs) >= 0) {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
        pixels.show();
        delay(1);
        pulseActive = false;
    }
}

// Mécanique du métronome côté ESP32
void updateMetronome(uint32_t now) {
    if (!running) return;
    uint32_t intervalMs = 60000UL / (uint32_t)bpm;

    // Un seul tick par passage (stable)
    if ((uint32_t)(now - lastStepMs) >= intervalMs) {
        lastStepMs += intervalMs;
        pulseOn();  // pulse LED + notifyBeat()
    }
}

// Gestion des boutons avec anti-rebond
bool pressedEdge(uint8_t pin, bool &lastState, uint32_t &lastChangeMs, uint32_t now) {
    bool reading = digitalRead(pin);
    if (reading != lastState && (now - lastChangeMs) > debounceMs) {
        lastChangeMs = now;
        lastState    = reading;
        if (reading == LOW) {
            return true; // front descendant = appui
        }
    }
    return false;
}

// (Re)lancer une nouvelle session bien calée
void startSession() {
    running        = true;
    beatCount      = 0;
    sessionStartMs = millis(); // On prend MAINTENANT comme origine de session
    lastStepMs     = millis(); // On commence immédiatement les ticks
    notifyStartTime();
    sendBPM();          // on renvoie aussi le BPM actuel
    notifyBeat();       // beatCount = 0
    Serial.println("Session started - LED devrait clignoter maintenant");
}

// Setup des boutons
void setupButtons() {
    pinMode(BTN_UP,   INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
}

// Setup BLE
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

  beatChar = service->createCharacteristic(
      BEAT_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  beatChar->setValue("0");

  startChar = service->createCharacteristic(
      START_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  startChar->setValue("0");

  positionChar = service->createCharacteristic(
      POSITION_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
   );
   positionChar->setValue("0");
   positionChar->addDescriptor(new NimBLE2904());

  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  //adv->setScanResponse(true);
  adv->start();
  Serial.println("BLE advertising started");
}

void setup() {
    NimBLEDevice::setTotalConnections(6);
    NimBLEDevice::setSecurityAuth(false, false, false);  // Pas de pairing (simplifie)
  Serial.begin(115200);
  delay(200);

  setupButtons();

  pixels.begin();
  pixels.clear();
  pixels.show();

  setupBle();

  // On démarre une session dès le boot
  startSession();
}

void sendPosition() {
    if (!running) return;
    
    uint32_t sessionTimeMs = millis() - sessionStartMs;
    uint32_t intervalMs = 60000UL / bpm;
    uint32_t positionInCycle = sessionTimeMs % (intervalMs * 4);  // Position dans 4 beats
    
    String s = String(positionInCycle);
    positionChar->setValue(s.c_str());
    positionChar->notify();
}

void loop() {
    uint32_t now = millis();

    // Gestion boutons : up/down BPM
    if (pressedEdge(BTN_UP, lastBtnUpState, lastBtnUpChangeMs, now)) {
        bpm++;
        if (bpm > 300) bpm = 300;
        sendBPM();

        if (running) {
            // On relance proprement la session avec le nouveau BPM
            startSession();
        }
    }

    if (pressedEdge(BTN_DOWN, lastBtnDownState, lastBtnDownChangeMs, now)) {
        bpm--;
        if (bpm < 20) bpm = 20;
        sendBPM();

        if (running) {
            startSession();
        }
    }

    updateMetronome(now);
    updatePulse(now);

    // DEBUG : affiche le statut toutes les 5 secondes
    static uint32_t lastDebug = 0;
    if (now - lastDebug > 5000) {
        lastDebug = now;
        Serial.printf("running=%d, bpm=%d, beatCount=%lu, now=%lu, lastStep=%lu\n", 
                      running, bpm, beatCount, now, lastStepMs);
    }

    delay(1);
}