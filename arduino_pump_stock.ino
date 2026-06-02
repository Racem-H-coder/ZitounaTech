int relayPin = 7;

// HC-SR04
int trigPin = 9;
int echoPin = 10;

long duration = 0;
float distance = 0.0f;
unsigned long cycleCounter = 0; // 1 cycle = 1 liter
bool cycleArmed = true;

void setup() {
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // pump OFF (relay active LOW)

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  Serial.begin(9600);
  delay(300);
  Serial.println("ARDUINO_READY");
}

void loop() {
  // Trigger HC-SR04
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Read echo with timeout to avoid blocking forever
  duration = pulseIn(echoPin, HIGH, 30000UL);
  distance = duration * 0.034f / 2.0f;

  Serial.print("Distance: ");
  Serial.println(distance);

  const bool objectDetected = (distance > 0.0f && distance <= 4.0f);

  if (objectDetected && cycleArmed) {
    Serial.println("Pump ON");
    digitalWrite(relayPin, LOW);  // ON
    delay(4000);                  // one complete fill cycle (1 liter)
    digitalWrite(relayPin, HIGH); // OFF
    Serial.println("Pump OFF");

    cycleCounter++;
    Serial.print("PUMP_CYCLE_COMPLETE:");
    Serial.println(cycleCounter);

    // Prevent repeated triggers while the same bottle/object stays in place.
    cycleArmed = false;
  } else {
    digitalWrite(relayPin, HIGH); // keep OFF
  }

  // Rearm only when the object is removed from detection zone.
  if (!objectDetected && !cycleArmed) {
    cycleArmed = true;
    Serial.println("READY_FOR_NEXT_BOTTLE");
  }

  delay(150);
}
