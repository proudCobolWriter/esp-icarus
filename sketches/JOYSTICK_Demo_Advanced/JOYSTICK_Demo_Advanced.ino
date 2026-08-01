#define X_AXIS_PIN 32
#define Y_AXIS_PIN 33

constexpr uint8_t SAMPLE_SIZE = 10;
typedef uint8_t r_arr[SAMPLE_SIZE];

r_arr x_readings = {0};
r_arr y_readings = {0};

uint8_t calibration_offsets[2] = {0, 0};
float calibration_scales[2] = {1.0f, 1.0f};

void setup() {
	Serial.begin(115200);

	while (!Serial)
		;

	Serial.println(F("Thumb Joystick demo (advanced version with calibration)"));

	pinMode(X_AXIS_PIN, INPUT);
	pinMode(Y_AXIS_PIN, INPUT);

	// Sets the ESP32 ADC resolution to 8-bit so (0-255) range
	analogReadResolution(8);

	Serial.println(F("Get ready for the calibration in 2 seconds..."));

	delay(2000);

	calibrate();

	delay(5000);
}

void calibrate() {
	Serial.println(F("Starting calibration. Actuate the joystick in all directions for 5 seconds"));

	uint8_t x_min, x_max, y_min, y_max;
	x_min = x_max = y_min = y_max = 128;

	uint32_t start_timestamp = millis();
	uint16_t time_elapsed = 0;

	while (time_elapsed < 5000) {
		uint8_t x = analogRead(X_AXIS_PIN);
		uint8_t y = analogRead(Y_AXIS_PIN);

		if (x < x_min) {
			x_min = x;
		}

		if (x > x_max) {
			x_max = x;
		}

		if (y < y_min) {
			y_min = y;
		}

		if (y > y_max) {
			y_max = y;
		}

		delay(10);

		time_elapsed = millis() - start_timestamp;
	}

	Serial.printf("Found the following extremums: X-min: %d | X-max: %d\n", x_min, x_max);
	Serial.print(F("                               "));
	Serial.printf("Y-min: %d | Y-max: %d", y_min, y_max);
	Serial.println();

	// the range of values taken by one side (positive or negative) of the axis
	uint8_t x_avg_delta = (x_max - x_min) / 2;
	uint8_t y_avg_delta = (y_max - y_min) / 2;

	// prevents zero division
	if (x_avg_delta == 0) {
		x_avg_delta = 128;
	}

	if (y_avg_delta == 0) {
		y_avg_delta = 128;
	}

	uint8_t x_mid = (x_max + x_min) / 2;
	uint8_t y_mid = (y_max + y_min) / 2;

	calibration_offsets[0] = x_mid;
	calibration_offsets[1] = y_mid;

	float x_scale = 128.0f / x_avg_delta;
	float y_scale = 128.0f / y_avg_delta;

	calibration_scales[0] = x_scale;
	calibration_scales[1] = y_scale;

	Serial.println(F("Calibration run complete!"));
	Serial.println(F("---------------------------------------------------"));
	Serial.printf("Middle at X: %d | Y: %d\n", x_mid, y_mid);
	Serial.printf("Value ranges: X: %d wide | Y: %d wide\n", x_avg_delta * 2, y_avg_delta * 2);
	Serial.printf("Scale factors: X: %.2f | Y: %.2f\n", x_scale, y_scale);
}

template<typename T>
T clamp(const T v, const T min, const T max) {
    return (v <= min) ? min : ((v >= max) ? max : v);
}

void insert_at(r_arr arr, uint8_t n, uint8_t pos, uint8_t val) {
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

	int16_t sum = 0;
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

void applyOffsets(int8_t *x, int8_t *y) {
    constexpr int8_t max_bound = 127;
    constexpr int8_t min_bound = -128;

    int8_t x_offseted = (*x) + calibration_offsets[0];
    int8_t y_offseted = (*y) + calibration_offsets[1];

    int8_t x_scaled = x_offseted * calibration_scales[0];
    int8_t y_scaled = y_offseted * calibration_scales[1];

	(*x) = x_scaled;//clamp<int8_t>(x_scaled, min_bound, max_bound);
	(*y) = y_scaled;//clamp<int8_t>(y_scaled, min_bound, max_bound);
}

void loop() {
	int16_t x = analogRead(X_AXIS_PIN);
	int16_t y = analogRead(Y_AXIS_PIN);

	applyOffsets(&x, &y);

	insert_at(x_readings, SAMPLE_SIZE, 0, x);
	insert_at(y_readings, SAMPLE_SIZE, 0, y);

	float x_mean = get_mean(x_readings, SAMPLE_SIZE);
	float y_mean = get_mean(y_readings, SAMPLE_SIZE);

	float x_variance = get_variance(x_readings, SAMPLE_SIZE, x_mean);
	float y_variance = get_variance(y_readings, SAMPLE_SIZE, y_mean);

	Serial.printf("X:%d\nY:%d\n", x, y);
	//Serial.printf("Average_X:%.2f\nAverage_Y:%.2f\n", x_mean, y_mean);
	//Serial.printf("Variance_X:%.2f\nVariance_Y:%.2f\n", x_variance, y_variance);

	delay(100);
}
