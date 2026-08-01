#include "Wire.h"

#include "Adafruit_BMP280.h"
#include "Adafruit_MPL3115A2.h"
#include "Adafruit_Sensor.h"

#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define BMP_ADDR 0x76
#define MPL_ADDR 0x60

#define MSL_PRESSURE_HPA 1013.25

Adafruit_BMP280 bmp;
Adafruit_MPL3115A2 mpl;

// State variables
float pressure = 0.0;
float temp = 0.0;
float altitude = 0.0;

float pressureSum = 0.0;
float tempSum = 0.0;
float altitudeSum = 0.0;

int totalIterations = 0;

// Configuration
bool useSerialPlotter = false;
unsigned long delayTime = 1000; // BMP280 has a 1Hz refresh rate;

byte pingDevice(uint8_t address) {
	Wire.beginTransmission(address);
	return Wire.endTransmission();
}

void setup() {
	Serial.begin(115200);
	while (!Serial)
		;

	Serial.println(F("Starting I2C bus..."));

	Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
	Wire.setWireTimeout(3000, true);

	while (!bmp.begin(0x76)) {
		Serial.println(F("Could not find a valid BMP280 sensor, check wiring. "
		                 "Retrying in 500 ms"));
		delay(500);
	}

	while (!mpl.begin()) {
		Serial.println(F("Could not find a valid MPL3115A2 sensor, check wiring. "
		                 "Retrying in 500 ms"));
		delay(500);
	}

	mpl.setSeaPressure(MSL_PRESSURE_HPA);
}

void loop() {
	byte error = pingDevice(MPL_ADDR);

	if (error == 0) {
		if (useSerialPlotter) {
			setSensorData();

			totalIterations++;

			tempSum += temp;
			pressureSum += pressure;
			altitudeSum += altitude;

			rawPrintSensorData();
		} else {
			prettyPrintSensorData();
		};
	} else {
		Serial.println(F("Connection lost! Attempting to recover..."));

		if (error == 5) {
			Serial.println(F("It was a timeout"));
		}

		delay(100);

		Wire.clearWireTimeoutFlag();

		if (mpl.begin()) {
			Serial.println(F("Reconnected successfully!"));
		}
	}

	delay(delayTime);
}

void setSensorData() {
    // Only does the MPL, I don't remember why I did that, but the API of the Adafruit MPL library is basically 1:1 to the BMP library anyways. Will have to find a way to do sensor fusion and take both sensors into consideration.

	// Which method is better? The mpl.getXXXXX() function wrapper or the mpl.getLastConversionResults() function one?

	mpl.startOneShot();

	unsigned long time = millis();
	while (!mpl.conversionComplete() && millis() - time < 200) {
		delay(10);
	}

	pressure = mpl.getLastConversionResults(MPL3115A2_PRESSURE); // or simply mpl.getPressure();
	temp = mpl.getLastConversionResults(MPL3115A2_TEMPERATURE);  // or simply mpl.getTemperature();
	altitude = mpl.getLastConversionResults(MPL3115A2_ALTITUDE); // or simply mpl.getAltitude();
}

void rawPrintSensorData() {
	Serial.print(F("Pressure:"));
	Serial.println(pressure);
	Serial.print(F("Pressure_average:"));
	Serial.println(pressureSum / (float)totalIterations);
	Serial.print(F("Temperature:"));
	Serial.println(temp);
	Serial.print(F("Temperature_average:"));
	Serial.println(tempSum / (float)totalIterations);
	Serial.print(F("Altitude:"));
	Serial.println(altitude);
	Serial.print(F("Altitude_average:"));
	Serial.println(altitudeSum / (float)totalIterations);
}

void prettyPrintSensorData() {
    // includes both sensors
	Serial.println(F("=== BMP ==="));

	Serial.println(F("Temperature = "));
	Serial.print(bmp.readTemperature());
	Serial.println("°C");

	Serial.println(F("Pressure = "));
	Serial.print(bmp.readPressure() / 100.0f);
	Serial.println("hPa");

	Serial.println(F("Rough altitude = "));
	Serial.print(bmp.readAltitude(MSL_PRESSURE_HPA));
	Serial.println("m");

	Serial.println("");

	Serial.println(F("=== MPL ==="));

	Serial.print(F("Temperature = "));
	Serial.print(mpl.getTemperature());
	Serial.println("°C");

	Serial.println(F("Pressure = "));
	Serial.print(mpl.getPressure());
	Serial.println("hPa");

	Serial.println(F("Rough altitude = "));
	Serial.print(mpl.getAltitude());
	Serial.println("m");

	Serial.println("");
}