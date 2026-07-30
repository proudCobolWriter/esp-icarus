// Look at this example for an even more robust implementation
// https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/WebServer/WebServer.ino

#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "FreeRTOS.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <QMC5883LCompass.h>
#include <TinyGPSPlus.h>
#include <WiFi.h>
#include <time.h>

#include "Secrets.h"
#include "SerialProxy.h"

// CONSTANTS

const char *filesDir = "/serve/";
const char *indexPath = "/serve/views/index.html";
const char *notFoundPath = "/serve/views/404.html";

const char *ntpServer = "pool.ntp.org";

const unsigned short WEB_SERVER_PORT = 80U; // default HTTP protocol port
const bool IGNORE_WIFI_EVENTS = true;       // disable for debugging

const uint8_t MAX_RETRIES = 5; // max WiFi router initial login attempts

const unsigned short GPS_LOG_DELAY = 5;
const unsigned short CLEANUP_DELAY = 5;
const unsigned short STATUS_DELAY = 1;

const int RX_PIN = 16, TX_PIN = 17;
const uint8_t SERIAL_PORT = 2; // 0 is used for code uploading and serial monitor, 1 is used on custom
                               // pins only, 2 is more versatile
const uint32_t GPS_BAUD = 9600;

const bool SYSTEM_MONITORING_DEBUG = false; // decides whether or not we print every STATUS_DELAY second a detailed
                                            // overview of all the running tasks on the system

// INSTANTIATING

AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket ws("/ws");

HardwareSerial gpsSerial(SERIAL_PORT);

JsonDocument liveStream;    // up-to-date sensor information retrieved every
                            // GPS_LOG_DELAY interval
JsonDocument historyStream; // old sensors information retrieved only once upon
                            // socket handshake

TinyGPSPlus gps;
QMC5883LCompass compass;

esp_chip_info_t chip_info;

// TASK HANDLES

TaskHandle_t SysMonTaskHandle = NULL;
TaskHandle_t GPSProcessTaskHandle = NULL;
TaskHandle_t CompassProcessTaskHandle = NULL;
TaskHandle_t BroadcastStatusTaskHandle = NULL;
TaskHandle_t WebSocketCleanupTaskHandle = NULL;

QueueHandle_t telemetryQueue;

// STRUCT DEFS

struct TaskRunTimeState {
	TaskHandle_t handle;
	uint32_t lastRunTime;
};

struct SystemTelemetry {
	float totalCpuLoad;
	uint32_t activeTaskCount;
	uint32_t freeHeapBytes;
};

// VARIABLES

bool fsOK = false;

unsigned long lastGpsTime;
unsigned long epochTime;

// WEB SERVER

unsigned long getTime() {
	time_t now;
	struct tm timeinfo;

	if (!getLocalTime(&timeinfo)) {
		Serial.println(F("[-] Failed to obtain epoch time (from NTP server)"));
		return (0);
	}

	time(&now);
	return (unsigned long)now;
}

String getFormattedTime() {
	time_t now;
	struct tm timeinfo;

	time(&now);
	localtime_r(&now, &timeinfo);

	char buffer[25];
	strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", &timeinfo);

	return String(buffer);
}

void initSensorJson() {
	JsonObject gpsLive = liveStream["sensors"]["gps"].to<JsonObject>();

	gpsLive["lat"] = 0.0;
	gpsLive["lon"] = 0.0;
	gpsLive["alt"] = 0.0;
	gpsLive["epoch"] = 0.0;

	historyStream["sensors"]["gps"]["pastPoints"].to<JsonArray>();
}

template <typename T> void logNewPoint(T lat, T lon, T alt, unsigned long int time = 0) {
	if (time == 0)
		time = getTime();

	JsonObject gpsLive = liveStream["sensors"]["gps"].as<JsonObject>();

	gpsLive["lat"] = lat;
	gpsLive["lon"] = lon;
	gpsLive["alt"] = alt;
	gpsLive["epoch"] = time;
}

template <typename T> void logPastPoint(T lat, T lon, T alt, unsigned long int time = 0) {
	if (time == 0)
		time = getTime();

	JsonArray pastPoints = historyStream["sensors"]["gps"]["pastPoints"].as<JsonArray>();

	if (pastPoints.size() >= 50) {
		pastPoints.remove(0);
	};

	JsonObject point = pastPoints.add<JsonObject>();
	point["lat"] = lat;
	point["lon"] = lon;
	point["alt"] = alt;
	point["epoch"] = time;
}

String stringifyDocument(const JsonDocument &doc) {
	String jsonOutput;
	jsonOutput.reserve(measureJson(doc) + 1);
	serializeJson(doc, jsonOutput);
	return jsonOutput;
}

void notifyClients() { ws.textAll("SENSORS:" + stringifyDocument(liveStream)); }

constexpr unsigned int imperativeHash(const char *str) {
	// constexpr needed for switch case statements, because hashes needs to be evaluated at compile time
	unsigned int hash = 0;

	for (size_t i = 0; str[i] != '\0'; i++) {
		hash ^= ((hash << 5) + (unsigned char)str[i] + (hash >> 2));
	}

	return (hash & 0x7FFFFFFF);
}

void handleWebSocketMessage(AsyncWebSocketClient *client, AwsFrameInfo *info, uint8_t *data, size_t len) {
	Serial.printf("[+] Received WS Packet. Length: %d, Opcode: %d\n", info->len, info->opcode);

	if (info->final && info->index == 0 && info->len == len) {
		String message((char *)data, len);

		auto messageHash = imperativeHash(message.c_str());

		switch (messageHash) {
		case imperativeHash("retrieveAllLogs"): {
			String rawPayload = "[";
			rawPayload.reserve(4096); // Same as before, prevents heap fragmentation

			auto appendLog = [&rawPayload](const String &log) {
				rawPayload += "\"";

				for (size_t j = 0; j < log.length(); j++) {
					char c = log[j];

					if (c == '"')
						rawPayload += "\\\"";
					else if (c == '\n')
						rawPayload += "\\n";
					else if (c == '\r')
						rawPayload += "\\r";
					else
						rawPayload += c;
				}

				rawPayload += "\",";
			};

			if (historyWrapped) {
				for (size_t i = historyIndex; i < MAX_LOG_HISTORY; i++) {
					appendLog(logHistory[i]);
				}
			}

			size_t limit = historyWrapped ? historyIndex : logHistory.size();
			for (size_t i = 0; i < limit; i++) {
				appendLog(logHistory[i]);
			}

			if (rawPayload.endsWith(",")) {
				rawPayload.remove(rawPayload.length() - 1);
			}

			rawPayload += "]";

			client->text("LOGS-HISTORY:" + rawPayload);
			break;
		}
		case imperativeHash("retrieveReadings"): {
			client->text("SENSORS-HISTORY:" + stringifyDocument(historyStream));
			break;
		}
		default:
			Serial.printf("Unknown command hash executed: %u (%s)\n", messageHash, message.c_str());
			break;
		}
	}
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
             size_t len) {
	switch (type) {
	case WS_EVT_CONNECT:
		Serial.printf("[+] WebSocket client #%u connected from %s\n", client->id(),
		              client->remoteIP().toString().c_str());
		break;
	case WS_EVT_DISCONNECT:
		Serial.printf("[-] WebSocket client #%u disconnected\n", client->id());
		break;
	case WS_EVT_DATA: {
		AwsFrameInfo *info = (AwsFrameInfo *)arg;
		if (info->opcode == WS_TEXT) {
			data[len] = 0;
			handleWebSocketMessage(client, info, data, len);
		}
		break;
	}
	case WS_EVT_PING:
		break;
	case WS_EVT_PONG:
		break;
	case WS_EVT_ERROR: {
		uint16_t error_code = *((uint16_t *)arg);
		const __FlashStringHelper *reason;

		// All WS error codes there: https://github.com/Luka967/websocket-close-codes
		switch (error_code) {
		case 1001:
			reason = F("Going Away (Server shutting down)");
			break;
		case 1002:
			reason = F("Protocol Error");
			break;
		case 1003:
			reason = F("Unsupported Data");
			break;
		case 1005:
			reason = F("No Status Rcvd");
			break;
		case 1006:
			reason = F("Abnormal Closure (Lost connection/RST)");
			break;
		case 1009:
			reason = F("Message Too Big");
			break;
		default:
			reason = F("Unknown WebSocket Error");
			break;
		}

		Serial.printf("[-] WebSocket client #%u experienced an error of code %u: %s\n", client->id(), error_code,
		              reason);
		break;
	};
	default:
		break;
	}
}

void initWebServer() {
	server.begin();
	Serial.println(F("[+] HTTP Async Server online!"));
}

void initWebSocket() {
	ws.onEvent(onEvent);
	server.addHandler(&ws);
	Serial.println(F("[+] WebSocket Handlers registered!"));
}

void initRouteHandling() {
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/dashboard"); });

	server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (LittleFS.exists(indexPath)) {
			AsyncWebServerResponse *response = request->beginResponse(LittleFS, indexPath, "text/html");

			if (response != nullptr) {
				response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
				request->send(response);
				return;
			} else {
				Serial.println(F("[-] Null pointer exception whilst handling client dashboard GET request"));
			}
		}

		request->send(500, "text/plain", F("Internal Error: index file missing from LittleFS"));

		Serial.println(F("[-] Internal Error: index file missing from LittleFS"));
	});

	AsyncStaticWebHandler &handler = server.serveStatic("/", LittleFS, filesDir);

	handler.setCacheControl("public, max-age=31536000"); // year long valid caching

	server.onNotFound([](AsyncWebServerRequest *request) {
		if (request->method() == HTTP_OPTIONS) {
			request->send(200);
			return;
		}

		if (LittleFS.exists(notFoundPath)) {
			request->send(LittleFS, notFoundPath, "text/html", false);
			return;
		}

		request->send(500, "text/plain", F("Internal Error: 404 error html file missing from LittleFS"));

		Serial.println(F("[-] Internal Error: 404 error html file missing from LittleFS"));
	});
}

void getNetworkInfo() {
	if (WiFi.status() == WL_CONNECTED) {
		Serial.print(F("[*] Network information for "));
		Serial.println(SECRET_SSID);

		Serial.println("[+] BSSID : " + WiFi.BSSIDstr());
		Serial.print(F("[+] Gateway IP : "));
		Serial.println(WiFi.gatewayIP().toString());
		Serial.print(F("[+] Subnet Mask : "));
		Serial.println(WiFi.subnetMask().toString());
		Serial.println((String) "[+] RSSI : " + WiFi.RSSI() + " dB");
		Serial.print(F("[+] ESP32 IP : "));
		Serial.println(WiFi.localIP().toString());
	}
}

void scanWiFi() {
	Serial.println(F("Scan start"));
	// WiFi.scanNetworks will return the number of networks found.
	int n = WiFi.scanNetworks();

	Serial.println(F("Scan done"));

	if (n == 0) {
		Serial.println(F("No networks found"));
	} else {
		Serial.print(n);
		Serial.println(F(" networks found"));
		Serial.println(F("Nr | SSID                             | RSSI | CH "
		                 "| Encryption"));

		for (int i = 0; i < n; ++i) {
			// Print SSID and RSSI for each network found
			Serial.printf("%2d", i + 1);
			Serial.print(" | ");
			Serial.printf("%-32.32s",
			              WiFi.SSID(i).c_str()); // %s means string formatting. - means left
			                                     // padding. -32 enforces atleast 32
			                                     // characters, if there are some missing it
			                                     // fills the gaps out with spaces. .32 means
			                                     // it will truncate the text past 32 chars.
			Serial.print(" | ");
			Serial.printf("%4" PRIi32, WiFi.RSSI(i));
			Serial.print(" | ");
			Serial.printf("%2" PRIi32, WiFi.channel(i));
			Serial.print(" | ");

			switch (WiFi.encryptionType(i)) {
			case (WIFI_AUTH_OPEN):
				Serial.print(F("open"));
				break;
			case (WIFI_AUTH_WEP):
				Serial.print(F("WEP"));
				break;
			case (WIFI_AUTH_WPA_PSK):
				Serial.print(F("WPA"));
				break;
			case (WIFI_AUTH_WPA2_PSK):
				Serial.print(F("WPA2"));
				break;
			case (WIFI_AUTH_WPA_WPA2_PSK):
				Serial.print(F("WPA+WPA2"));
				break;
			case (WIFI_AUTH_WPA2_ENTERPRISE):
				Serial.print(F("WPA2-EAP"));
				break;
			case (WIFI_AUTH_WPA3_PSK):
				Serial.print(F("WPA3"));
				break;
			case (WIFI_AUTH_WPA2_WPA3_PSK):
				Serial.print(F("WPA2+WPA3"));
				break;
			case (WIFI_AUTH_WAPI_PSK):
				Serial.print(F("WAPI"));
				break;
			default:
				Serial.print(F("unknown"));
			}

			Serial.println();

			delay(10);
		}
	}

	// Delete the scan result to free memory for code below.
	WiFi.scanDelete();
	Serial.println(F("-------------------------------------"));
}

void printChipInfo() {
	Serial.println(F("******************************************************"));
	Serial.println(F("[*] Printing information about the running chip:"));

	// const int bytes_to_kb = 1000;
	// const int bytes_to_mb = 1000 * 1000;

	const int bytes_to_kib = 1024;
	const int bytes_to_mib = 1024 * 1024;

	Serial.printf("[+] Chip name : %s\n", getPrettyChipModel(chip_info.model));
	Serial.printf("[+] CPU frequency : %u MHz\n", ESP.getCpuFreqMHz());
	Serial.printf("[+] CPU cores : %d\n", chip_info.cores);
	Serial.printf("[+] Flash size : %u MiB\n", ESP.getFlashChipSize() / bytes_to_mib);
	Serial.printf("[+] Free heap size (DRAM) : %u KiB\n", ESP.getFreeHeap() / bytes_to_kib);
	Serial.printf("[+] Tick rate : %d Hz\n", configTICK_RATE_HZ);
}

const char *getPrettyChipModel(esp_chip_model_t model) {
	switch (model) {
	case CHIP_ESP32:
		return "ESP32";
	case CHIP_ESP32S2:
		return "ESP32-S2";
	case CHIP_ESP32S3:
		return "ESP32-S3";
	case CHIP_ESP32C3:
		return "ESP32-C3";
	case CHIP_ESP32C2:
		return "ESP32-C2";
	case CHIP_ESP32C6:
		return "ESP32-C6";
	case CHIP_ESP32H2:
		return "ESP32-H2";
	case CHIP_ESP32P4:
		return "ESP32-P4";
	case CHIP_POSIX_LINUX:
		return "POSIX/Linux Simulator";
	default:
		return "Unknown Model";
	}
}

const char *getPrettyAuthModeName(wifi_auth_mode_t authmode) {
	switch (authmode) {
	case WIFI_AUTH_OPEN:
		return "Open";
	case WIFI_AUTH_WEP:
		return "WEP";
	case WIFI_AUTH_WPA_PSK:
		return "WPA";
	case WIFI_AUTH_WPA2_PSK:
		return "WPA2";
	case WIFI_AUTH_WPA_WPA2_PSK:
		return "WPA/WPA2";
	case WIFI_AUTH_WPA2_ENTERPRISE:
		return "WPA2 Enterprise";
	case WIFI_AUTH_WPA3_PSK:
		return "WPA3";
	case WIFI_AUTH_WPA2_WPA3_PSK:
		return "WPA2/WPA3";
	case WIFI_AUTH_WAPI_PSK:
		return "WAPI";
	default:
		return "Unknown";
	}
}

void WiFiEventHandler(WiFiEvent_t event) { Serial.printf("Got Event: %d\n", event); }

void initWiFi() {
	esp_log_level_set("wifi", ESP_LOG_NONE);

	uint8_t retryCount = 0;

	WiFi.mode(WIFI_STA);
	WiFi.persistent(false);
	// Don't save the WiFi settings to flash memory (NVS)

	Serial.println(F("******************************************************"));

	if (!IGNORE_WIFI_EVENTS)
		WiFi.onEvent(WiFiEventHandler);

	while (WiFi.status() != WL_CONNECTED && retryCount < MAX_RETRIES) {
		retryCount++;
		Serial.printf("[+] Connecting to %s (Attempt %d/%d)...", SECRET_SSID, retryCount, MAX_RETRIES);

		WiFi.disconnect(true);
		delay(100);

		WiFi.begin(SECRET_SSID, SECRET_WPA_PASS);

		uint8_t timeoutCounter = 0;
		while (WiFi.status() != WL_CONNECTED && timeoutCounter < 20) {
			delay(250);
			Serial.print(".");
			timeoutCounter++;
		}
		Serial.println("");

		if (WiFi.status() != WL_CONNECTED) {
			Serial.println(F("[-] Association failed or timed out. Backing off..."));
			delay(retryCount * 2000);
		}
	}

	if (WiFi.status() != WL_CONNECTED) {
		Serial.println(F("[-] CRITICAL: WiFi connection failed completely. Sleeping..."));
		while (true)
			delay(500);
	}

	Serial.println(F("[+] Successfully connected to the WiFi network!"));
	Serial.println(F("******************************************************"));

	getNetworkInfo();
}

void initGPS() {
	gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);

	Serial.println(F("---------------------------------------------------------"));
	Serial.println(F("[+] BN-880 GPS module has been initialized"));
	Serial.print(F("[+] Using library "));
	Serial.println(TinyGPSPlus::libraryVersion());
	Serial.println(F("[+] by Mikal Hart"));
	Serial.println(F("---------------------------------------------------------"));
}

void initCompass() {
	compass.init();                          // handles 0x0B and 0x09 registers internally
	compass.setMode(0x01, 0x00, 0x10, 0x00); // check the library documentation or the chip's manual for information
	// compass.setCalibration(-1442, -27, -1567, -243, -1000, 1000);

	Serial.println(F("---------------------------------------------------------"));
	Serial.println(F("[+] QMC5883L Magnetometer module has been initialized"));
	Serial.println(F("[+] Using library QMC5883LCompass 1.2.3"));
	Serial.println(F("[+] by MPrograms"));
	Serial.println(F("---------------------------------------------------------"));
}

void displayGPSInfo(decltype(millis()) time) {
	Serial.print(F("Location: "));
	if (gps.location.isValid()) {
		double lat = gps.location.lat(), lon = gps.location.lng(), alt = gps.altitude.meters();
		epochTime = getTime();

		Serial.print(lat, 6);
		Serial.print(F(","));
		Serial.print(lon, 6);
		Serial.println();
		Serial.print(F("Number of satellites: "));
		Serial.println(gps.satellites.value());

		auto distanceToHomeplate = (double)TinyGPSPlus::distanceBetween(lat, lon, CALIBRATION_LAT, CALIBRATION_LON);
		Serial.printf("Distance to homeplate: %lf\n", distanceToHomeplate);

		logNewPoint(lat, lon, alt, epochTime);
		logPastPoint(lat, lon, alt, epochTime);
		notifyClients();
	} else {
		Serial.println(F("INVALID"));
	}

	return;

	Serial.print(F("  Date/Time: "));
	if (gps.date.isValid()) {
		Serial.print(gps.date.month());
		Serial.print(F("/"));
		Serial.print(gps.date.day());
		Serial.print(F("/"));
		Serial.print(gps.date.year());
	} else {
		Serial.print(F("INVALID"));
	}

	Serial.print(F(" "));
	if (gps.time.isValid()) {
		if (gps.time.hour() < 10)
			Serial.print(F("0"));
		Serial.print(gps.time.hour());
		Serial.print(F(":"));
		if (gps.time.minute() < 10)
			Serial.print(F("0"));
		Serial.print(gps.time.minute());
		Serial.print(F(":"));
		if (gps.time.second() < 10)
			Serial.print(F("0"));
		Serial.print(gps.time.second());
		Serial.print(F("."));
		if (gps.time.centisecond() < 10)
			Serial.print(F("0"));
		Serial.print(gps.time.centisecond());
	} else {
		Serial.print(F("INVALID"));
	}

	Serial.println();
}

void broadcastStatus() {
	if (WiFi.status() != WL_CONNECTED)
		return;

	JsonDocument statusDoc;
	JsonObject wifi = statusDoc["wifi"].to<JsonObject>();
	JsonObject esp = statusDoc["esp"].to<JsonObject>();

	wifi["name"] = WiFi.BSSIDstr();
	wifi["gateway"] = WiFi.gatewayIP().toString();
	wifi["subnetMask"] = WiFi.subnetMask().toString();
	wifi["localIP"] = WiFi.localIP().toString();
	wifi["rssi"] = WiFi.RSSI();
	wifi["encryption"] = "Unknown";

	wifi_ap_record_t ap_record;
	if (esp_wifi_sta_get_ap_info(&ap_record) == ESP_OK) {
		wifi["encryption"] = getPrettyAuthModeName(ap_record.authmode);
	}

	statusDoc["clients"] = ws.count();

	esp["model"] = getPrettyChipModel(chip_info.model);

	ws.textAll("STATUS:" + stringifyDocument(statusDoc));
}

bool strPrefix(const char *pre, const char *str) { return strncmp(pre, str, strlen(pre)) == 0; }

void setup() {
	Serial.begin(115200);
	while (!Serial)
		;

	if (!LittleFS.begin(true)) {
		Serial.println(F("[-] Failure while mounting LittleFS!"));
		return;
	}

	fsOK = true;
	Serial.println(F("[+] LittleFS mounted successfully."));

	esp_chip_info(&chip_info);
	printChipInfo();

	initWiFi();
	delay(500);

	configTime(0, 0, ntpServer);
	initSensorJson();

	initGPS();
	initCompass();

	initWebSocket();
	initRouteHandling();
	initWebServer();

	telemetryQueue = xQueueCreate(1, sizeof(SystemTelemetry));

	xTaskCreatePinnedToCore(GPSProcessTask, "GPSProcessTask", 8192, NULL, 1, &GPSProcessTaskHandle, 1);
	xTaskCreatePinnedToCore(CompassProcessTask, "CompassProcessTask", 8192, NULL, 1, &CompassProcessTaskHandle, 1);
	xTaskCreatePinnedToCore(BroadcastStatusTask, "BroadcastStatusTask", 8192, NULL, 1, &BroadcastStatusTaskHandle, 1);
	xTaskCreatePinnedToCore(WebSocketCleanupTask, "WebSocketCleanupTask", 8192, NULL, 1, &WebSocketCleanupTaskHandle,
	                        1);
	xTaskCreatePinnedToCore(SysMonTask, "SysMonTask", 4096, NULL, 1, &SysMonTaskHandle, 0);

	vTaskDelete(NULL);
}

void loop() {
	// Code never reached
}

void GPSProcessTask(void *pvParameters) {
	for (;;) {
		unsigned long time = millis();

		if (time > 15000 && gps.charsProcessed() < 10) {
			Serial.println(F("[-] No GPS detected: check wiring."));
			vTaskDelete(NULL);
		}

		while (gpsSerial.available() > 0) {
			if (gps.encode(gpsSerial.read())) {
				if (time - lastGpsTime > GPS_LOG_DELAY * 1000) {
					lastGpsTime = time;
					Serial.println(F("[+] GPS cycle"));
					displayGPSInfo(time);
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

void CompassProcessTask(void *pvParameters) {
	for (;;) {
		compass.read();
		Serial.println(compass.getAzimuth());

        vTaskDelay(pdMS_TO_TICKS(250));
	}
}

void WebSocketCleanupTask(void *pvParameters) {
	for (;;) {
		ws.cleanupClients();
		vTaskDelay(pdMS_TO_TICKS(CLEANUP_DELAY * 1000));
	}
}

void BroadcastStatusTask(void *pvParameters) {
	SystemTelemetry received_data;

	for (;;) {
		if (telemetryQueue != NULL) {
			if (xQueueReceive(telemetryQueue, &received_data, portMAX_DELAY) == pdTRUE) {
				if (SYSTEM_MONITORING_DEBUG)
					Serial.printf("TOTAL CPU USAGE: %.1f%%\n", received_data.totalCpuLoad);
				broadcastStatus();
			};
		} else {
			vTaskDelay(pdMS_TO_TICKS(100));
		}
	}
}

void SysMonTask(void *pvParameters) {
	uint32_t oldUlTotalRunTime = 0;

	static std::vector<TaskRunTimeState> history;

	for (;;) {
		UBaseType_t uxArraySize = uxTaskGetNumberOfTasks();

		TaskStatus_t *pxTaskStatusArray = (TaskStatus_t *)pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
		TaskSnapshot_t *pxTaskSnapshotArray = (TaskSnapshot_t *)pvPortMalloc(uxArraySize * sizeof(TaskSnapshot_t));

		if (pxTaskStatusArray != NULL && pxTaskSnapshotArray != NULL) {
			uint32_t ulTotalRunTime;
			uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

			const uint32_t totalSystemDelta = ulTotalRunTime - oldUlTotalRunTime;

			UBaseType_t tcbSize;
			uxTaskGetSnapshotAll(pxTaskSnapshotArray, uxArraySize, &tcbSize);

			if (SYSTEM_MONITORING_DEBUG) {
				Serial.println(F(
				    "------------------------------ FREE RTOS TASK MONITOR SNAPSHOT -------------------------------"));
				Serial.printf("%-20s | %-4s | %-9s | %-8s | %-12s | %-12s | %-12s\n", "Task Name", "Core", "CPU Usage",
				              "Priority", "Alloc Stack", "Free Stack", "Stack Usage");
				Serial.println(F(
				    "----------------------------------------------------------------------------------------------"));
			}

			float totalCpuPercent = 0.0f;

			for (UBaseType_t i = 0; i < uxArraySize; i++) {
				const TaskStatus_t &status = pxTaskStatusArray[i];
				uint32_t totalStackBytes = 0;

				for (UBaseType_t j = 0; j < uxArraySize; j++) {
					if (pxTaskSnapshotArray[j].pxTCB == status.xHandle) {
						totalStackBytes =
						    (uintptr_t)pxTaskSnapshotArray[j].pxEndOfStack - (uintptr_t)status.pxStackBase;
						break;
					}
				}

				const UBaseType_t freeStack = status.usStackHighWaterMark;
				const UBaseType_t allocatedStack = totalStackBytes;
				const UBaseType_t usedBytesStack = (allocatedStack > freeStack) ? (allocatedStack - freeStack) : 0;
				const float usedPercent =
				    (allocatedStack > 0) ? ((float)usedBytesStack / allocatedStack) * 100.0f : 0.0f;

				constexpr UBaseType_t invalidCoreInt = 2147483647;
				int8_t core = (status.xCoreID == invalidCoreInt) ? -1 : status.xCoreID;

				uint32_t prevRunTime = 0;
				bool foundInHistory = false;

				for (TaskRunTimeState &record : history) {
					if (record.handle == status.xHandle) {
						prevRunTime = record.lastRunTime;
						record.lastRunTime = status.ulRunTimeCounter;
						foundInHistory = true;
						break;
					}
				}

				if (!foundInHistory) {
					history.push_back({status.xHandle, status.ulRunTimeCounter});
					prevRunTime = status.ulRunTimeCounter;
				}

				float cpuPercent = 0.0f;
				if (totalSystemDelta > 0 && status.ulRunTimeCounter >= prevRunTime) {
					const uint32_t taskDelta = status.ulRunTimeCounter - prevRunTime;
					cpuPercent = (((float)taskDelta / (float)totalSystemDelta * 100.0f) /
					              portNUM_PROCESSORS); // portNUM_PROCESSORS should be equal to 2 with the Xtensa ESP32
					                                   // CPU. It normalizes the total system load.
				}

				if (!strPrefix("IDLE", status.pcTaskName)) {
					totalCpuPercent += cpuPercent;

					if (SYSTEM_MONITORING_DEBUG) {
						Serial.printf("%-20s | %-4d | %8.1f%% | %-8u | %-10u B | %-10u B | %10.1f%%\n",
						              status.pcTaskName, core, cpuPercent, status.uxCurrentPriority, allocatedStack,
						              usedBytesStack, usedPercent);
					}
				}
			}

			for (std::vector<TaskRunTimeState>::iterator it = history.begin(); it != history.end();) {
				bool stillAlive = false;

				for (UBaseType_t i = 0; i < uxArraySize; i++) {
					if (pxTaskStatusArray[i].xHandle == it->handle) {
						stillAlive = true;
						break;
					}
				}

				if (!stillAlive) {
					it = history.erase(it);
				} else {
					++it;
				}
			}

			vPortFree(pxTaskStatusArray);
			vPortFree(pxTaskSnapshotArray);

			oldUlTotalRunTime = ulTotalRunTime;

			SystemTelemetry data = {totalCpuPercent, uxArraySize, ESP.getFreeHeap()};
			xQueueOverwrite(telemetryQueue, &data);
		}

		vTaskDelay(pdMS_TO_TICKS(STATUS_DELAY * 1000));
	}
}