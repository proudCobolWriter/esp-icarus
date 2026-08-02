// The following code could be used to improve the ADC accuracy, but in that case
// the arbitrary unit analogRead returns should be dropped and mV/V used
// https://github.com/G6EJD/ESP32-ADC-Accuracy-Improvement-function/blob/master/ESP32_ADC_Read_Voltage_Accurate.ino

// Another thing that I could do to improve the accuracy is introducing scale factors for both sides of the axes
// To do so I'd need to turn the calibration_scales array into a 2x2 matrix essentially, calculate some more deltas,
// edit the applyOffsets function and voila, but cba to do it rn

#define X_AXIS_PIN 32
#define Y_AXIS_PIN 33

#define ADC_RESOLUTION 12 // Sets the ESP32 ADC resolution to 12-bit so (0-4095) range

constexpr int16_t MID_POINT = static_cast<int16_t>(powf(2.0f, (float)ADC_RESOLUTION) / 2);
constexpr uint8_t SAMPLE_SIZE = 10;

typedef int16_t r_arr[SAMPLE_SIZE];

r_arr x_readings = {0};
r_arr y_readings = {0};

uint16_t deadzones[2] = {0, 0};

float calibration_offsets[2] = {0.0f, 0.0f};
float calibration_scales[2] = {1.0f, 1.0f};

void setup() {
	Serial.begin(115200);

	while (!Serial)
		;

	Serial.println(F("Thumb Joystick demo (advanced version with calibration)"));

	pinMode(X_AXIS_PIN, INPUT);
	pinMode(Y_AXIS_PIN, INPUT);

	analogReadResolution(ADC_RESOLUTION);

	Serial.println(F("Get ready for the calibration in 2 seconds..."));
	delay(2000);

	calibrate();
	delay(5000);
}

void calibrate() {
	Serial.println(F("Starting calibration. Actuate the joystick in all directions for 5 seconds"));

	int16_t x_min = MID_POINT, x_max = MID_POINT;
	int16_t y_min = MID_POINT, y_max = MID_POINT;

	uint32_t start_timestamp = millis();

	while (millis() - start_timestamp < 5000) {
		int16_t x = analogRead(X_AXIS_PIN);
		int16_t y = analogRead(Y_AXIS_PIN);

		if (x < x_min)
			x_min = x;
		if (x > x_max)
			x_max = x;
		if (y < y_min)
			y_min = y;
		if (y > y_max)
			y_max = y;

		delay(10);
	}

	Serial.printf("Found the following extremums: X-min: %d | X-max: %d\n", x_min, x_max);
	Serial.print(F("                               "));
	Serial.printf("Y-min: %d | Y-max: %d", y_min, y_max);
	Serial.println();

	// calculating midpoints (dropped this method and instead we are using the real mean values the joystick sits at,
	// but requires an extra step of calibration as seen below) float x_mid = (x_max + x_min) / 2.0f; float y_mid =
	// (y_max + y_min) / 2.0f;

	// the range of values taken by one side (positive or negative) of the axis
	float x_avg_delta = (x_max - x_min) / 2.0f;
	float y_avg_delta = (y_max - y_min) / 2.0f;

	// prevents zero division
	if (x_avg_delta == 0.0f)
		x_avg_delta = MID_POINT;
	if (y_avg_delta == 0.0f)
		y_avg_delta = MID_POINT;

	float x_scale = MID_POINT / x_avg_delta;
	float y_scale = MID_POINT / y_avg_delta;

	Serial.println(F("Next calibration step. Hold the stick still for 5 seconds, checking noise data"));
	delay(1000);

	start_timestamp = millis();

	uint16_t count = 0;

	float x_mean = 0.0f, x_M2 = 0.0f;
	float y_mean = 0.0f, y_M2 = 0.0f;

	while (millis() - start_timestamp < 5000) {
		int16_t x = analogRead(X_AXIS_PIN);
		int16_t y = analogRead(Y_AXIS_PIN);

		count++;

		// Welford update for X
		float x_delta = x - x_mean;
		x_mean += x_delta / count;
		x_M2 += x_delta * (x - x_mean);

		// Welford update for Y
		float y_delta = y - y_mean;
		y_mean += y_delta / count;
		y_M2 += y_delta * (y - y_mean);

		delay(10);
	}

	constexpr float SIGMA_MULTIPLIER = 3.0f;

	float x_variance = (count > 1) ? x_M2 / (count - 1) : 0.0f;
	float y_variance = (count > 1) ? y_M2 / (count - 1) : 0.0f;

	float x_deviation = sqrtf(x_variance);
	float y_deviation = sqrtf(y_variance);

	calibration_offsets[0] = x_mean;
	calibration_offsets[1] = y_mean;

	calibration_scales[0] = x_scale;
	calibration_scales[1] = y_scale;

	deadzones[0] = static_cast<uint16_t>(roundf(x_deviation * calibration_scales[0] * SIGMA_MULTIPLIER));
	deadzones[1] = static_cast<uint16_t>(roundf(y_deviation * calibration_scales[1] * SIGMA_MULTIPLIER));

	Serial.println(F("Calibration run complete!"));
	Serial.println(F("---------------------------------------------------"));
	Serial.printf("Middle at X: %.0f | Y: %.0f\n", calibration_offsets[0], calibration_offsets[1]);
	Serial.printf("Value ranges: X: %.0f wide | Y: %.0f wide\n", x_avg_delta * 2, y_avg_delta * 2);
	Serial.printf("Scale factors: X: %.2f | Y: %.2f\n", calibration_scales[0], calibration_scales[1]);
	Serial.printf("Deadzone: X: %d | Y: %d\n", deadzones[0], deadzones[1]);
}

void insert_at(r_arr arr, uint8_t n, uint8_t pos, int16_t val) {
	if (n == 0 || pos >= n)
		return;

	for (uint8_t i = n - 1; i > pos; i--) {
		arr[i] = arr[i - 1];
	}
	arr[pos] = val;
}

float get_mean(const r_arr arr, uint8_t n) {
	if (n == 0) {
		return 0.0f;
	}

	int32_t sum = 0;
	for (uint8_t i = 0; i < n; i++) {
		sum += arr[i];
	}

	return (float)sum / n;
}

float get_variance(const r_arr arr, uint8_t n, float mean) {
	if (n <= 1)
		return 0.0f;

	float sumSquaredDifferences = 0;
	for (uint8_t i = 0; i < n; i++) {
		float diff = arr[i] - mean;
		sumSquaredDifferences += diff * diff; // multiplication is faster than powers for computers so
	}

	return sumSquaredDifferences / (n - 1);
}

void applyOffsets(int16_t *x, int16_t *y) {
	float x_scaled = roundf((*x - calibration_offsets[0]) * calibration_scales[0]);
	float y_scaled = roundf((*y - calibration_offsets[1]) * calibration_scales[1]);

	*x = std::clamp<int16_t>(x_scaled, -MID_POINT, MID_POINT);
	*y = std::clamp<int16_t>(y_scaled, -MID_POINT, MID_POINT);
}

void applyDeadzone(int16_t *x, int16_t *y) {
	if (abs(*x) <= deadzones[0])
		*x = 0;
	if (abs(*y) <= deadzones[1])
		*y = 0;
}

void loop() {
	int16_t x = analogRead(X_AXIS_PIN);
	int16_t y = analogRead(Y_AXIS_PIN);

	applyOffsets(&x, &y);
	applyDeadzone(&x, &y);

	insert_at(x_readings, SAMPLE_SIZE, 0, x);
	insert_at(y_readings, SAMPLE_SIZE, 0, y);

	float x_mean = get_mean(x_readings, SAMPLE_SIZE);
	float y_mean = get_mean(y_readings, SAMPLE_SIZE);

	float x_variance = get_variance(x_readings, SAMPLE_SIZE, x_mean);
	float y_variance = get_variance(y_readings, SAMPLE_SIZE, y_mean);

	float x_deviation = sqrtf(x_variance);
	float y_deviation = sqrtf(y_variance);

	Serial.printf("X:%d\nY:%d\n", x, y);
	Serial.printf("Average_X:%.2f\nAverage_Y:%.2f\n", x_mean, y_mean);
	Serial.printf("Variance_X:%.2f\nVariance_Y:%.2f\n", x_variance, y_variance);
	Serial.printf("Deviation_X:%.2f\nDeviation_Y:%.2f\n", x_deviation, y_deviation);

	delay(100);
}
