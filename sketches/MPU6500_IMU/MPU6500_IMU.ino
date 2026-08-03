// Turns out I had a counterfeit MPU6500 instead of a MPU6050 lol, thankfully it's technically a better sensor and even
// has a temperature reading!

#include "FastIMU.h"
#include <Wire.h>

#define IMU_ADDRESS 0x68    // Change to the address of the IMU
#define PERFORM_CALIBRATION // Comment to disable startup calibration
MPU6500 IMU;                // Change to the name of any supported IMU!

// Currently supported IMUS: MPU9255 MPU9250 MPU6886 MPU6500 MPU6050 ICM20689 ICM20690 ICM20948 BMI055 BMX055 BMI160
// LSM6DS3 LSM6DSL QMI8658

calData calib = {0}; // Calibration data
AccelData accelData; // Sensor data
GyroData gyroData;
MagData magData;

struct attitudeData {
	float roll = 0.0f;
	float pitch = 0.0f;
	float yaw = 0.0f;
} attitude;

unsigned long last_timestamp = 0;

void setup() {
	Serial.begin(115200);
	while (!Serial)
		;

	Wire.begin();
	Wire.setClock(400000); // in kHz

	int err = IMU.init(calib, IMU_ADDRESS);
	if (err != 0) {
		Serial.print("Error initializing IMU: ");
		Serial.println(err);
		while (true)
			;
	}

#ifdef PERFORM_CALIBRATION
	Serial.println("FastIMU calibration & data example");

	if (IMU.hasMagnetometer()) {
		delay(1000);
		Serial.println("Move IMU in figure 8 pattern until done.");
		delay(1000);
		IMU.calibrateMag(&calib);
		Serial.println("Magnetic calibration done!");
	}

	delay(1000);
	Serial.println("Keep IMU level.");
	delay(1000);
	IMU.calibrateAccelGyro(&calib);
	Serial.println("Calibration done!");
	Serial.println("Accel biases X/Y/Z: ");
	Serial.print(calib.accelBias[0]);
	Serial.print(", ");
	Serial.print(calib.accelBias[1]);
	Serial.print(", ");
	Serial.println(calib.accelBias[2]);
	Serial.println("Gyro biases X/Y/Z: ");
	Serial.print(calib.gyroBias[0]);
	Serial.print(", ");
	Serial.print(calib.gyroBias[1]);
	Serial.print(", ");
	Serial.println(calib.gyroBias[2]);
	if (IMU.hasMagnetometer()) {
		Serial.println("Mag biases X/Y/Z: ");
		Serial.print(calib.magBias[0]);
		Serial.print(", ");
		Serial.print(calib.magBias[1]);
		Serial.print(", ");
		Serial.println(calib.magBias[2]);
		Serial.println("Mag Scale X/Y/Z: ");
		Serial.print(calib.magScale[0]);
		Serial.print(", ");
		Serial.print(calib.magScale[1]);
		Serial.print(", ");
		Serial.println(calib.magScale[2]);
	}
	delay(1000);
	IMU.init(calib, IMU_ADDRESS);
#endif

	// err = IMU.setGyroRange(500);      // USE THESE TO SET THE RANGE, IF AN INVALID RANGE IS SET IT WILL RETURN -1
	// err = IMU.setAccelRange(2);       // THESE TWO SET THE GYRO RANGE TO ±500 DPS AND THE ACCELEROMETER RANGE TO ±2g

	if (err != 0) {
		Serial.print("Error Setting range: ");
		Serial.println(err);
		while (true) {
			;
		}
	}

	last_timestamp = millis();
}

void loop() {
	unsigned long current_timestamp = millis();
	float dt = (current_timestamp - last_timestamp) / 1000.0f;
	last_timestamp = current_timestamp;

	IMU.update();
	IMU.getAccel(&accelData);
	IMU.getGyro(&gyroData);

	// Serial.print(accelData.accelX);
	// Serial.print("\t");
	// Serial.print(accelData.accelY);
	// Serial.print("\t");
	// Serial.print(accelData.accelZ);
	// Serial.print("\t");
	//
	// Serial.print(gyroData.gyroX);
	// Serial.print("\t");
	// Serial.print(gyroData.gyroY);
	// Serial.print("\t");
	// Serial.print(gyroData.gyroZ);
	// Serial.print("\t");
	//
	// if (IMU.hasMagnetometer()) {
	//	IMU.getMag(&magData);
	//	Serial.print(magData.magX);
	//	Serial.print("\t");
	//	Serial.print(magData.magY);
	//	Serial.print("\t");
	//	Serial.print(magData.magZ);
	//	Serial.print("\t");
	//}
	//
	// if (IMU.hasTemperature()) {
	//	Serial.print(IMU.getTemp());
	//}
	// Serial.println();

	float accelRoll = atan2(accelData.accelY, -accelData.accelZ) * 180.0f / M_PI;
	float accelPitch =
	    atan2(accelData.accelX, sqrtf(accelData.accelY * accelData.accelY + accelData.accelZ * accelData.accelZ)) *
	    180.0f / M_PI;

	float factor = 0.95f;

	attitude.roll = factor * (attitude.roll + gyroData.gyroX * dt) + (1.0f - factor) * accelRoll;
	attitude.pitch = factor * (attitude.pitch + gyroData.gyroY * dt) + (1.0f - factor) * accelPitch;

	attitude.yaw += gyroData.gyroZ * dt;

	Serial.printf("Roll:%.2f\n", attitude.roll);
	Serial.printf("Pitch:%.2f\n", attitude.pitch);
	Serial.printf("Yaw:%.2f\n", attitude.yaw);

	delay(50); // ~20Hz loop
}
