#include <QMC5883LCompass.h>
#include <cmath>

QMC5883LCompass compass;

unsigned long lastTimestamp = 0;
const unsigned long sampleInterval = 100; // 1/10 * 1000 ms

struct st_RawCompassData {
	int x;
	int y;
	int z;
};

const float HI_CALIBRATION[3] = {-3265.6f, -1396.6f, -19787.7f};

const float SI_CALIBRATION[3][3] = {{1.088f, 0.060f, -0.145f}, {0.060f, 1.108f, 0.139f}, {-0.145f, 0.139f, 0.870f}};

//  | MODE CONTROL (MODE)     | Value |
//  | ----------------------- | ----- |
//  | Standby		          | 0x00  |
//  | Continuous	          | 0x01  |
//
//  | OUTPUT DATA RATE (ODR)  | Value |
//  | ----------------------- | ----- |
//  | 10Hz		              | 0x00  |
//  | 50Hz		              | 0x04  |
//  | 100Hz		              | 0x08  |
//  | 200Hz		              | 0x0C  |
//
//  | FULL SCALE (RNG)        | Value |
//  | ----------------------- | ----- |
//  | 2G			          | 0x00  | Higher sensitivity
//  | 8G			          | 0x10  | Lower sensitivity
//
//  | OVER SAMPLE RATIO (OSR) | Value |
//  | ----------------------- | ----- |
//  | 64			          | 0xC0  |
//  | 128			          | 0x80  |
//  | 256			          | 0x40  |
//  | 512			          | 0x00  |

void setup() {
	Serial.begin(115200);

	// Initialize the sensor (Handles 0x0B and 0x09 registers internally)
	compass.init();
	compass.setMode(0x01, 0x00, 0x00, 0x00);

	// Optional: If you want to use the specific OSR/Baud settings from the
	// manual: compass.setADDR(0x0D);

	// Apply the Hard Iron offsets we calculated earlier
	// This tells the library: "The center isn't 0,0; it's here."

	compass.clearCalibration();
	// compass.setCalibration(-7965, 1925, -5755, 3660, -26000, -13002);
	// compass.setCalibrationOffsets(HI_CALIBRATION[0], HI_CALIBRATION[1], HI_CALIBRATION[2]);
	// compass.setCalibrationScales(1.0, 1.0, 1.0);

	Serial.println("QMC5883L Library Test Initialized");
}

void loop() {
	unsigned long currentMillis = millis();

	if (currentMillis - lastTimestamp >= sampleInterval) {
		unsigned long delta = currentMillis - lastTimestamp;
		lastTimestamp = currentMillis;

		compass.read();

		struct st_RawCompassData raw;
		raw.x = compass.getX();
		raw.y = compass.getY();
		raw.z = compass.getZ();

		float precal[3] = {(float)raw.x - HI_CALIBRATION[0], (float)raw.y - HI_CALIBRATION[1],
		                   (float)raw.z - HI_CALIBRATION[2]};
		float cal[3] = {0.0f, 0.0f, 0.0f};

		for (uint8_t i = 0; i < 3; i++) {
			for (uint8_t j = 0; j < 3; j++) {
				cal[i] += SI_CALIBRATION[i][j] * precal[j];
			};
		};

		double azimuth;
		azimuth = -1.0 * atan2(cal[1], cal[0]) * 180.0 / PI; // atan2(y, x)
		if (azimuth < 0)
			azimuth += 360.0;

		Serial.print("Raw:");
		Serial.printf("0,0,0,0,0,0,%d,%d,%d\r\n", raw.x / 10, raw.y / 10, raw.z / 10);

		Serial.print("Uni:");
		Serial.printf("0,0,0,0,0,0,%.2f,%.2f,%.2f\r\n", cal[0], cal[1], cal[2]);

		Serial.print("Azimuth: ");
		Serial.println(azimuth);

		if (delta > 0) {
			Serial.printf("Frequency: %luHz", (unsigned long)(1000 / delta));
			Serial.println();
		}
	}
}
