#ifndef ARDUINO_CODE_BRIDGE_H
#define ARDUINO_CODE_BRIDGE_H

#include <QString>

namespace ArduinoCodeBridge {

// Build an Arduino sketch string with a fixed code.
QString buildKeypadLcdSketch(const QString &secretCode);

// Validate a keypad code against expected employee code.
bool isCodeAccepted(const QString &enteredCode, const QString &expectedCode);

} // namespace ArduinoCodeBridge

#endif // ARDUINO_CODE_BRIDGE_H
