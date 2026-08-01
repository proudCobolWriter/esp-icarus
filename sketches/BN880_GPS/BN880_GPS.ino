// Open this repo for more detailed examples
// https://github.com/TinyuZhao/TinyGPSPlus-ESP32/tree/master/examples

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

#define UART_BAUD_RATE 9600
#define UART_TX_PIN 17
#define UART_RX_PIN 16

HardwareSerial GPS_Serial(2);
TinyGPSPlus gps;

void setup() {
	Serial.begin(115200);
	while (!Serial)
		;

	Serial.println(F("Starting UART bus..."));
	Serial.println(F("--- BN-880 GPS Test ---"));

	GPS_Serial.begin(UART_BAUD_RATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
}

void loop() {
	while (GPS_Serial.available() > 0) {
		char c = GPS_Serial.read();

		if (gps.encode(c)) {
			if (gps.location.isUpdated()) {
				Serial.print(F("Latitude: "));
				Serial.print(gps.location.lat(), 6);
				Serial.print("°");
				Serial.print(F(", Longitude: "));
				Serial.print(gps.location.lng(), 6);
				Serial.print("°");

				if (gps.altitude.isValid()) {
					Serial.print(F(", Altitude: "));
					Serial.print(gps.altitude.meters(), 2);
					Serial.println("m");
				} else {
					Serial.println(F(", Altitude: INVALID"));
				}
			}
		}
	}
}