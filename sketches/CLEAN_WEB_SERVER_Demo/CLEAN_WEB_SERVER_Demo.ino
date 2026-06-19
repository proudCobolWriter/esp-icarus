// Look at this example for an even more robust implementation
// https://github.com/espressif/arduino-esp32/blob/master/libraries/WebServer/examples/WebServer/WebServer.ino

#include "esp_log.h"
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
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

const unsigned short TIMER_DELAY = 5;
const unsigned short CLEANUP_DELAY = 5;
const unsigned short GPS_LOG_DELAY = 5;

static const int RX_PIN = 16, TX_PIN = 17;
static const uint8_t SERIAL_PORT = 2; // 0 is used for code uploading and serial monitor, 1 is used on custom
// pins only, 2 is more versatile
static const uint32_t GPS_BAUD = 9600;

// INSTANTIATING

AsyncWebServer server(WEB_SERVER_PORT);
AsyncWebSocket ws("/ws");

HardwareSerial gpsSerial(SERIAL_PORT);

JsonDocument liveStream;    // up-to-date sensor information retrieved every
                            // TIMER_DELAY interval
JsonDocument historyStream; // old sensors information retrieved only once upon
                            // socket handshake

TinyGPSPlus gps;

// VARIABLES

bool fsOK = false;

unsigned long lastTime = 0;
unsigned long lastCleanup = 0;
unsigned long lastHistoryLogTime = 0;

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

String stringifyTelemetry(const JsonDocument &doc) {
	String jsonOutput;
	jsonOutput.reserve(measureJson(doc) + 1);
	serializeJson(doc, jsonOutput);
	return jsonOutput;
}

void notifyClients() { ws.textAll("SENSORS:" + stringifyTelemetry(liveStream)); }

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
			client->text("SENSORS-HISTORY:" + stringifyTelemetry(historyStream));
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
		Serial.println(WiFi.gatewayIP());
		Serial.print(F("[+] Subnet Mask : "));
		Serial.println(WiFi.subnetMask());
		Serial.println((String) "[+] RSSI : " + WiFi.RSSI() + " dB");
		Serial.print(F("[+] ESP32 IP : "));
		Serial.println(WiFi.localIP());
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

	initWiFi();
	delay(500);

	configTime(0, 0, ntpServer);
	initSensorJson();

	initGPS();

	initWebSocket();
	initRouteHandling();
	initWebServer();
}

void loop() {
	unsigned long time = millis();

	if (time > 15000 && gps.charsProcessed() < 10) {
		Serial.println(F("[-] No GPS detected: check wiring."));
		while (true)
			;
	}

	while (gpsSerial.available() > 0) {
		if (gps.encode(gpsSerial.read())) {
			if (time - lastTime > TIMER_DELAY * 1000) {
				lastTime = millis();
				Serial.println(F("[+] GPS cycle"));
				displayGPSInfo(time);
			}
		}
	}

	if (time - lastCleanup > CLEANUP_DELAY * 1000) {
		ws.cleanupClients();
		lastCleanup = millis();
	}
}