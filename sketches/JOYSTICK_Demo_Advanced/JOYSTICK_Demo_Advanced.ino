#include <map>

#define X_AXIS_PIN 32
#define Y_AXIS_PIN 33

constexpr uint8_t SAMPLE_SIZE = 10;
constexpr uint8_t LINEARIZATION_STEPS = 101; // uneven number to have an integer median value

typedef uint8_t r_arr[SAMPLE_SIZE];

r_arr x_readings = {0};
r_arr y_readings = {0};

uint8_t calibration_offsets[2] = {0, 0};
float calibration_scales[2] = {1.0f, 1.0f};

float x_linearization_factors[LINEARIZATION_STEPS] = {1.0f};
float y_linearization_factors[LINEARIZATION_STEPS] = {1.0f};

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

	unsigned long last_timestamp = millis();
	unsigned long time_elapsed = 0;

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

		unsigned long current_timestamp = millis();
		unsigned long delta = current_timestamp - last_timestamp;
		last_timestamp = current_timestamp;

		time_elapsed += delta;
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

	Serial.println(
	    F("Next calibration step. Push the joystick in a single direction at a constant rate for 5 seconds"));

	unsigned long start_timestamp = millis();
	unsigned long duration = 0;

	std::map<uint32_t, float> readings_dict;

	while (true) {
		int16_t x = analogRead(X_AXIS_PIN);
		int16_t y = analogRead(Y_AXIS_PIN);

		applyOffsets(&x, &y, false);

		if (abs(y) <= 128.0f * 0.05f)
			continue;

		float percent = y / 128.0f;

		duration = millis() - start_timestamp;

		readings_dict.insert(std::pair<uint32_t, float>(duration, percent));

		if (abs(y) >= 128.0f * 0.95f)
			break;

		delay(100);
	}

	if (duration > 0) {
		for (auto it : readings_dict) {
			uint32_t key = it.first;
			float value = it.second;

			int16_t index = round((value / 2.0f + 0.5f) * (LINEARIZATION_STEPS - 1));

			if (index < 0) {
				index = 0;
			} else if (index > LINEARIZATION_STEPS - 1) {
				index = LINEARIZATION_STEPS - 1;
			}

			float progress = ((float)key / duration) * 2.0f - 1.0f;
			float factor = (value != 0.0f) ? progress / value : 1.0f;

			Serial.println(index);
			Serial.println(progress);
			Serial.println(value);
			Serial.println(factor);
			Serial.println(F("-----"));

			y_linearization_factors[index] = factor;
		}
	}

	Serial.println(F("Calibration run complete!"));
	Serial.println(F("---------------------------------------------------"));
	Serial.printf("Middle at X: %d | Y: %d\n", x_mid, y_mid);
	Serial.printf("Value ranges: X: %d wide | Y: %d wide\n", x_avg_delta * 2, y_avg_delta * 2);
	Serial.printf("Scale factors: X: %.2f | Y: %.2f\n", x_scale, y_scale);
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

void applyOffsets(int16_t *x, int16_t *y, bool applyLinearization) {
	(*x) -= calibration_offsets[0];
	(*y) -= calibration_offsets[1];

	(*x) *= calibration_scales[0];
	(*y) *= calibration_scales[1];

	if (!applyLinearization)
		return;

	int16_t index = round((((*y) / 128.0f) / 2.0f + 0.5f) * (LINEARIZATION_STEPS - 1));

    Serial.println(index);

	if (index < 0) {
		index = 0;
	} else if (index > LINEARIZATION_STEPS - 1) {
		index = LINEARIZATION_STEPS - 1;
	}

    Serial.println(index);

	int16_t i1 = index;
    int16_t i2 = index;

	while (y_linearization_factors[i1] == 1.0f && y_linearization_factors[i2] == 1.0f) {
		i1 -= 1;
		i2 += 1;
	}

	if (y_linearization_factors[i1] == 1.0f) {
		index = i2;
	} else {
        index = i1;
    }

    Serial.println(index);

	(*y) *= y_linearization_factors[index];

    Serial.println(y_linearization_factors[index]);
}

void loop() {
	int16_t x = analogRead(X_AXIS_PIN);
	int16_t y = analogRead(Y_AXIS_PIN);

	applyOffsets(&x, &y, true);

	insert_at(x_readings, SAMPLE_SIZE, 0, x);
	insert_at(y_readings, SAMPLE_SIZE, 0, y);

	float x_mean = get_mean(x_readings, SAMPLE_SIZE);
	float y_mean = get_mean(y_readings, SAMPLE_SIZE);

	float x_variance = get_variance(x_readings, SAMPLE_SIZE, x_mean);
	float y_variance = get_variance(y_readings, SAMPLE_SIZE, y_mean);

	Serial.printf("X:%d\nY:%d\n", x, y);
	Serial.printf("Average_X:%.2f\nAverage_Y:%.2f\n", x_mean, y_mean);
	Serial.printf("Variance_X:%.2f\nVariance_Y:%.2f\n", x_variance, y_variance);

	delay(100);
}
