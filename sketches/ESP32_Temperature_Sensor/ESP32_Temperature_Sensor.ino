// But what helped the most was extending WiFi beacon intervals by calling esp_wifi_set_ps (WIFI_PS_MAX_MODEM).
// to reduce temperature from 49°C to 28°C (other than reducing clock speed)

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

void computeMandelbrotChunk() {
	const int width = 1080;
	const int height = 720;
	const int max_iterations = 500;

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			float zx = 0.0f;
			float zy = 0.0f;
			float cx = (x - width / 1.5f) * 4.0f / width;
			float cy = (y - height / 2.0f) * 4.0f / height;

			int iter = 0;
			while (zx * zx + zy * zy < 4.0f && iter < max_iterations) {
				float tmp = zx * zx - zy * zy + cx;
				zy = 2.0f * zx * zy + cy;
				zx = tmp;
				iter++;
			}
		}

		if (y % 10 == 0) {
			vTaskDelay(1);
		}
	}
}

float getLegacyCoreTemp() {
	uint8_t raw = temprature_sens_read();

	if (raw == 128 || raw == 0) {
		return -1.0f;
	}

	float temp_f = (float)raw;
	float temp_c = (temp_f - 32.0f) / 1.8f;
	return temp_c;
}

void setup() { Serial.begin(115200); }

void loop() {
	float temp = getLegacyCoreTemp();
	if (temp > 0) {
		Serial.printf("Estimated Core Temp: %.1f °C\n", temp);
	} else {
		Serial.println("Temp sensor unavailable/disabled");
	}

	computeMandelbrotChunk();
}