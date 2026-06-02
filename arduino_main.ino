#include <Keypad.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>

// ── AS608 (4 fils) ──────────────────────────
// VCC  → 3.3V
// GND  → GND
// TX   → Pin 2 (SoftwareSerial RX)
// RX   → Pin 3 (SoftwareSerial TX)
//
// ── LCD 16x2 I2C ────────────────────────────
// VCC -> 5V, GND -> GND, SDA -> A4, SCL -> A5 (UNO)
//
// ── HX-543 Clavier ──────────────────────────
// R1=9, R2=8, R3=7, R4=6
// C1=10, C2=4, C3=13
// (I2C libere les broches LCD paralleles)

SoftwareSerial mySerial(2, 3);  // RX=2, TX=3
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {10, 4, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum State {
  IDLE,
  ENTERING_PIN,
  ENROLLING_FINGER_1,
  WAITING_REMOVE,
  ENROLLING_FINGER_2,
  VERIFYING_FINGER
};

State g_state = IDLE;
String g_pinInput = "";
uint32_t g_stateStartedAt = 0;
uint32_t g_noFingerSince = 0;
int g_targetSlotId = -1;
bool g_cancelRequested = false;

const uint32_t PIN_TIMEOUT_MS = 60000;
const uint32_t ENROLL_TIMEOUT_MS = 30000;
const uint32_t VERIFY_TIMEOUT_MS = 10000;

bool initFingerprintSensor() {
  // UNO + SoftwareSerial: 57600 peut etre instable selon modules/cables.
  // Essai standard, puis fallback 9600.
  mySerial.begin(57600);
  finger.begin(57600);
  for (int i = 0; i < 5; ++i) {
    if (finger.verifyPassword()) return true;
    delay(220);
  }

  mySerial.end();
  delay(100);
  mySerial.begin(9600);
  finger.begin(9600);
  for (int i = 0; i < 5; ++i) {
    if (finger.verifyPassword()) return true;
    delay(220);
  }
  return false;
}

void updateLCD(const String &line1, const String &line2) {
  static String lastLine1 = "";
  static String lastLine2 = "";

  const String clipped1 = line1.substring(0, 16);
  const String clipped2 = line2.substring(0, 16);

  // Evite de clear/reprint l'ecran en boucle (flicker LCD visible).
  if (clipped1 == lastLine1 && clipped2 == lastLine2) {
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(clipped1);
  lcd.setCursor(0, 1);
  lcd.print(clipped2);

  lastLine1 = clipped1;
  lastLine2 = clipped2;
}

void resetToIdle() {
  g_state = IDLE;
  g_pinInput = "";
  g_noFingerSince = 0;
  g_targetSlotId = -1;
  g_cancelRequested = false;
  updateLCD("En attente Qt...", "");
}

int nextFreeSlot() {
  // 1) Quick check: if template count is known and lower than max,
  // we still scan slots because some clones return inconsistent counts.
  finger.getTemplateCount();
  const int templateCount = finger.templateCount;

  // 2) Main scan: NOTFOUND means free slot on most AS608/R30x sensors.
  // Some clones can return ENROLLMISMATCH on empty/invalid entries;
  // treat that as reusable as well.
  for (int slot = 1; slot <= 127; ++slot) {
    uint8_t r = finger.loadModel(slot);
    if (r == FINGERPRINT_NOTFOUND || r == FINGERPRINT_ENROLLMISMATCH) {
      return slot;
    }
  }

  // 3) Fallback for clone sensors with unstable loadModel responses:
  // if templateCount says "not full", reuse the next expected slot.
  if (templateCount >= 0 && templateCount < 127) {
    int candidate = templateCount + 1;
    if (candidate < 1) candidate = 1;
    if (candidate > 127) candidate = 127;
    return candidate;
  }

  return -1;
}

void handlePinEntry() {
  char key = keypad.getKey();
  if (!key) return;

  if (key >= '0' && key <= '9') {
    if (g_pinInput.length() < 4) {
      g_pinInput += key;
      Serial.print("DIGIT:");
      Serial.println(key);
      String mask = "";
      for (int i = 0; i < 4; ++i) {
        if (i < g_pinInput.length()) mask += "*";
        else mask += "_";
        if (i < 3) mask += " ";
      }
      updateLCD("Entrez votre PIN", mask);
    }
    return;
  }

  if (key == '*') {
    if (g_pinInput.length() > 0) {
      g_pinInput.remove(g_pinInput.length() - 1);
      Serial.println("BACKSPACE");
    }
    return;
  }

  if (key == '#' && g_pinInput.length() == 4) {
    Serial.println("CONFIRM");
    updateLCD("PIN valide", "Attente Qt...");
    delay(500);
    resetToIdle();
  }
}

void enrollFingerStep1() {
  updateLCD("Posez le doigt", "Scan 1...");
  uint8_t p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return;
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:scan1_image");
    resetToIdle();
    return;
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:scan1_template");
    resetToIdle();
    return;
  }

  Serial.println("SCAN1_OK");
  Serial.println("REMOVE_FINGER");
  g_state = WAITING_REMOVE;
  g_stateStartedAt = millis();
  updateLCD("Retirez doigt", "Puis reposez");
}

void enrollFingerStep2() {
  updateLCD("Reposez doigt", "Scan 2...");
  uint8_t p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return;
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:scan2_image");
    resetToIdle();
    return;
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:scan2_template");
    resetToIdle();
    return;
  }
  Serial.println("SCAN2_OK");

  p = finger.createModel();
  if (p == FINGERPRINT_PACKETRECIEVEERR) {
    // Liaison parfois instable: une 2e tentative suffit souvent.
    delay(120);
    p = finger.createModel();
  }
  if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("FINGER_ERROR:create_model_mismatch");
    // Le doigt du scan 2 n'est pas le meme (ou mal place): recommencer proprement.
    updateLCD("Doigt diff.", "Recommencez");
    delay(900);
    g_state = ENROLLING_FINGER_1;
    g_stateStartedAt = millis();
    updateLCD("Posez le doigt", "Scan 1...");
    return;
  }
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:create_model");
    resetToIdle();
    return;
  }

  p = finger.storeModel(g_targetSlotId);
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:store_model");
    resetToIdle();
    return;
  }

  Serial.print("FINGER_SAVED:");
  Serial.println(g_targetSlotId);
  updateLCD("Enregistre !", "Slot " + String(g_targetSlotId));
  delay(800);
  resetToIdle();
}

void verifyFinger() {
  updateLCD("Posez le doigt", "Verification...");
  uint8_t p = finger.getImage();
  if (p == FINGERPRINT_NOFINGER) return;
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:verify_image");
    resetToIdle();
    return;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("FINGER_ERROR:verify_template");
    resetToIdle();
    return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("MATCH:");
    Serial.print(finger.fingerID);
    Serial.print(":");
    Serial.println(finger.confidence);
    updateLCD("Bienvenue !", "ID " + String(finger.fingerID));
    delay(600);
    resetToIdle();
    return;
  }

  if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("NO_MATCH");
    updateLCD("Non reconnu", "Essayez encore");
    delay(600);
    resetToIdle();
    return;
  }

  Serial.println("FINGER_ERROR:verify_search");
  resetToIdle();
}

void parseSerialCommand() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;
  Serial.print("CMD:");
  Serial.println(cmd);

  if (cmd == "START_PIN") {
    g_state = ENTERING_PIN;
    g_pinInput = "";
    g_stateStartedAt = millis();
    updateLCD("Mode PIN", "_ _ _ _");
    return;
  }

  if (cmd == "START_FINGER") {
    g_targetSlotId = nextFreeSlot();
    if (g_targetSlotId < 1) {
      Serial.println("FINGER_ERROR:no_free_slot");
      return;
    }
    g_state = ENROLLING_FINGER_1;
    g_stateStartedAt = millis();
    updateLCD("Mode Empreinte", "Posez le doigt");
    return;
  }

  if (cmd == "VERIFY_FINGER") {
    g_state = VERIFYING_FINGER;
    g_stateStartedAt = millis();
    updateLCD("Login empreinte", "Posez le doigt");
    return;
  }

  if (cmd == "CANCEL_FINGER") {
    g_cancelRequested = true;
    Serial.println("FINGER_ERROR:cancelled");
    resetToIdle();
    return;
  }

  if (cmd.startsWith("DELETE_FINGER:")) {
    int slot = cmd.substring(String("DELETE_FINGER:").length()).toInt();
    if (slot >= 1 && slot <= 127) {
      uint8_t p = finger.deleteModel(slot);
      if (p == FINGERPRINT_OK) {
        Serial.print("FINGER_DELETED:");
        Serial.println(slot);
      } else {
        Serial.println("FINGER_ERROR:delete_failed");
      }
    }
    return;
  }

  if (cmd == "DELETE_ALL_FINGERS") {
    uint8_t p = finger.emptyDatabase();
    if (p == FINGERPRINT_OK) {
      Serial.println("FINGER_DELETED:ALL");
    } else {
      Serial.println("FINGER_ERROR:delete_all_failed");
    }
    resetToIdle();
    return;
  }
}

void handleTimeout() {
  const uint32_t now = millis();
  if (g_state == ENTERING_PIN && now - g_stateStartedAt > PIN_TIMEOUT_MS) {
    Serial.println("TIMEOUT");
    resetToIdle();
    return;
  }
  if ((g_state == ENROLLING_FINGER_1 || g_state == WAITING_REMOVE || g_state == ENROLLING_FINGER_2)
      && now - g_stateStartedAt > ENROLL_TIMEOUT_MS) {
    Serial.println("TIMEOUT");
    resetToIdle();
    return;
  }
  if (g_state == VERIFYING_FINGER && now - g_stateStartedAt > VERIFY_TIMEOUT_MS) {
    Serial.println("TIMEOUT");
    resetToIdle();
    return;
  }
}

void setup() {
  Serial.begin(9600);  // Liaison Qt <-> Arduino (pins 0/1)
  Wire.begin();
  lcd.init();
  lcd.backlight();

  if (initFingerprintSensor()) {
    Serial.println("READY");
    updateLCD("AS608 detecte", "Pret");
  } else {
    Serial.println("FINGER_ERROR:sensor_not_found");
    updateLCD("Capteur absent", "Verifier cablage");
  }
  delay(800);
  resetToIdle();
}

void loop() {
  parseSerialCommand();
  handleTimeout();

  if (g_cancelRequested) {
    resetToIdle();
    return;
  }

  switch (g_state) {
    case IDLE:
      break;
    case ENTERING_PIN:
      handlePinEntry();
      break;
    case ENROLLING_FINGER_1:
      enrollFingerStep1();
      break;
    case WAITING_REMOVE:
      // Debounce: exiger un retrait reel du doigt ~700ms
      // pour eviter un scan2 trop rapide qui provoque create_model mismatch.
      if (finger.getImage() == FINGERPRINT_NOFINGER) {
        if (g_noFingerSince == 0) {
          g_noFingerSince = millis();
        } else if (millis() - g_noFingerSince >= 700) {
          g_noFingerSince = 0;
          g_state = ENROLLING_FINGER_2;
          g_stateStartedAt = millis();
        }
      } else {
        g_noFingerSince = 0;
      }
      break;
    case ENROLLING_FINGER_2:
      enrollFingerStep2();
      break;
    case VERIFYING_FINGER:
      verifyFinger();
      break;
  }
}
