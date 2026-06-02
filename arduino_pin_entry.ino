#include <Keypad.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

byte rowPins[ROWS] = {9, 8, 7, 6}; // R1..R4
byte colPins[COLS] = {5, 4, 3};    // C1..C3

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum State { WAITING, ENTERING, COMPLETE, CONFIRMED };
State state = WAITING;
String pinInput = "";

void drawMask() {
  lcd.setCursor(0, 1);
  for (int i = 0; i < 4; ++i) {
    if (i < pinInput.length()) lcd.print("*");
    else lcd.print("_");
    if (i < 3) lcd.print(" ");
  }
}

void resetEntryUI() {
  pinInput = "";
  state = ENTERING;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Entrez votre code");
  drawMask();
}

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("En attente Qt...");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "START_PIN") {
      resetEntryUI();
    }
  }

  if (state == WAITING) return;

  char key = keypad.getKey();
  if (!key) return;

  if (key >= '0' && key <= '9') {
    if (pinInput.length() < 4) {
      pinInput += key;
      Serial.print("DIGIT:");
      Serial.println(key);
      drawMask();
      if (pinInput.length() == 4) {
        state = COMPLETE;
      }
    }
    return;
  }

  if (key == '*') {
    if (pinInput.length() > 0) {
      pinInput.remove(pinInput.length() - 1);
      Serial.println("BACKSPACE");
      drawMask();
      state = ENTERING;
    }
    return;
  }

  if (key == '#') {
    if (pinInput.length() == 4) {
      Serial.println("CONFIRM");
      state = CONFIRMED;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Code enregistre !");
      delay(2000);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("En attente Qt...");
      state = WAITING;
      pinInput = "";
    }
    return;
  }
}
