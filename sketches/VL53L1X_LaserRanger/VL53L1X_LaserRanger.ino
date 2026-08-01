#include "Adafruit_VL53L1X.h"

#define IRQ_PIN 25
#define XSHUT_PIN 26

Adafruit_VL53L1X vl53 = Adafruit_VL53L1X(XSHUT_PIN, IRQ_PIN);

void scanDevices() {
	byte error, address;
	int nDevices;

	Serial.println("Scanning...");

	nDevices = 0;
	for (address = 1; address < 127; address++) {
		// The i2c_scanner uses the return value of
		// the Write.endTransmisstion to see if
		// a device did acknowledge to the address.
		Wire.beginTransmission(address);
		error = Wire.endTransmission();

		if (error == 0) {
			Serial.print("I2C device found at address 0x");
			if (address < 16)
				Serial.print("0");
			Serial.print(address, HEX);
			Serial.println("  !");

			nDevices++;
		} else if (error == 4) {
			Serial.print("Unknown error at address 0x");
			if (address < 16)
				Serial.print("0");
			Serial.println(address, HEX);
		}
	}
	if (nDevices == 0)
		Serial.println("No I2C devices found\n");
	else
		Serial.println("done\n");
}

void setup() {
	Serial.begin(115200);
	while (!Serial)
		delay(10);

    scanDevices();

	Serial.println(F("Adafruit VL53L1X sensor demo"));

	Wire.begin();
	if (!vl53.begin(0x29, &Wire)) {
		Serial.print(F("Error on init of VL sensor: "));
		Serial.println(vl53.vl_status);
		while (1)
			delay(10);
	}
	Serial.println(F("VL53L1X sensor OK!"));

	Serial.print(F("Sensor ID: 0x"));
	Serial.println(vl53.sensorID(), HEX);

	if (!vl53.startRanging()) {
		Serial.print(F("Couldn't start ranging: "));
		Serial.println(vl53.vl_status);
		while (1)
			delay(10);
	}
	Serial.println(F("Ranging started"));

	// Valid timing budgets: 15, 20, 33, 50, 100, 200 and 500ms!
	vl53.setTimingBudget(50);
	Serial.print(F("Timing budget (ms): "));
	Serial.println(vl53.getTimingBudget());

	/*
	vl.VL53L1X_SetDistanceThreshold(100, 300, 3, 1);
	vl.VL53L1X_SetInterruptPolarity(0);
	*/
}

void loop() {
	int16_t distance;

	if (vl53.dataReady()) {
		// new measurement for the taking!
		distance = vl53.distance();
		if (distance == -1) {
			// something went wrong!
			Serial.print(F("Couldn't get distance: "));
			Serial.println(vl53.vl_status);
			return;
		}
		Serial.print(F("Distance: "));
		Serial.print(distance);
		Serial.println(" mm");

		// data is read out, time for another reading!
		vl53.clearInterrupt();
	}
}
