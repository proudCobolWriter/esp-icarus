#define X_AXIS_PIN 32
#define Y_AXIS_PIN 33

void setup() {
	Serial.begin(115200);

	while (!Serial)
		;

	Serial.println(F("Thumb Joystick demo (simple version)"));

	pinMode(X_AXIS_PIN, INPUT);
	pinMode(Y_AXIS_PIN, INPUT);

	// Sets the ESP32 ADC resolution to 8-bit so (0-255) range
	analogReadResolution(12);
}

void loop() {
	uint16_t x = analogRead(X_AXIS_PIN);
	uint16_t y = analogRead(Y_AXIS_PIN);

	Serial.printf("X:%d\nY:%d\n", x, y);

	delay(100);
}
