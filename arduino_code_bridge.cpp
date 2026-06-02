#include "arduino_code_bridge.h"

namespace ArduinoCodeBridge {

QString buildKeypadLcdSketch(const QString &secretCode)
{
    const QString finalCode = secretCode.trimmed().isEmpty() ? QStringLiteral("1234") : secretCode.trimmed();
    return QStringLiteral(
               "#include <Keypad.h>\n"
               "#include <LiquidCrystal_I2C.h>\n"
               "\n"
               "LiquidCrystal_I2C lcd(0x27, 16, 2);\n"
               "\n"
               "const byte ROWS = 4;\n"
               "const byte COLS = 3;\n"
               "\n"
               "char keys[ROWS][COLS] = {\n"
               "  {'1','2','3'},\n"
               "  {'4','5','6'},\n"
               "  {'7','8','9'},\n"
               "  {'*','0','#'}\n"
               "};\n"
               "\n"
               "byte rowPins[ROWS] = {9, 8, 7, 6};\n"
               "byte colPins[COLS] = {5, 4, 3};\n"
               "\n"
               "Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);\n"
               "\n"
               "String code = \"%1\";\n"
               "String input = \"\";\n"
               "\n"
               "void setup() {\n"
               "  lcd.init();\n"
               "  lcd.backlight();\n"
               "  Serial.begin(9600);\n"
               "  lcd.setCursor(0, 0);\n"
               "  lcd.print(\"Enter Code:\");\n"
               "}\n"
               "\n"
               "void loop() {\n"
               "  char key = keypad.getKey();\n"
               "  if (key) {\n"
               "    if (key == '#') {\n"
               "      Serial.print(\"CODE:\");\n"
               "      Serial.println(input);\n"
               "      lcd.clear();\n"
               "      if (input == code) {\n"
               "        lcd.print(\"Code Correct\");\n"
               "      } else {\n"
               "        lcd.print(\"Wrong Code\");\n"
               "      }\n"
               "      delay(2000);\n"
               "      lcd.clear();\n"
               "      lcd.print(\"Enter Code:\");\n"
               "      input = \"\";\n"
               "    } else if (key == '*') {\n"
               "      input = \"\";\n"
               "      lcd.setCursor(0, 1);\n"
               "      lcd.print(\"                \");\n"
               "    } else {\n"
               "      input += key;\n"
               "      lcd.setCursor(0, 1);\n"
               "      lcd.print(input);\n"
               "    }\n"
               "  }\n"
               "}\n")
        .arg(finalCode);
}

bool isCodeAccepted(const QString &enteredCode, const QString &expectedCode)
{
    return enteredCode.trimmed() == expectedCode.trimmed();
}

} // namespace ArduinoCodeBridge
