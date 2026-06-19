#include "SerialProxy.h"
#include <Arduino.h>
#include <vector>

extern AsyncWebSocket ws;

// CONSTANTS

const size_t MAX_LOG_HISTORY = 50;

// VARIABLES

std::vector<String> logHistory;
size_t historyIndex = 0;
bool historyWrapped = false;
bool lastMessageLineEnded = true;

void broadcastToWebSocket(const String &msg) {
	if (ws.count() == 0)
		return;

	ws.textAll("LOG:" + msg);
}

void broadcastToWebSocket(const std::vector<String> &logs) {
	if (ws.count() == 0 || logs.empty())
		return;

	String flattenedLogs = "[";
	flattenedLogs.reserve(4096);

	for (size_t i = 0; i < logs.size(); i++) {
		flattenedLogs += "\"";

		const char *p = logs[i].c_str();

		while (*p) {
			char c = *p++;

			if (c == '"')
				flattenedLogs += "\\\"";
			else if (c == '\n')
				flattenedLogs += "\\n";
			else if (c == '\r')
				flattenedLogs += "\\r";
			else
				flattenedLogs += c;
		}

		flattenedLogs += "\"";
		if (i < logs.size() - 1) {
			flattenedLogs += ",";
		}
	}

	flattenedLogs += "]";

	ws.textAll("LOGS:" + flattenedLogs);
}

void addLogToHistory(const String &msg) {
	if (msg.length() == 0)
		return;

	size_t startPos = 0;
	std::vector<String> logs;

	if (!logHistory.empty()) {
		size_t lastIdx =
		    historyWrapped ? (historyIndex == 0 ? MAX_LOG_HISTORY - 1 : historyIndex - 1) : (logHistory.size() - 1);

		String &lastLog = logHistory[lastIdx];

		if (lastLog.length() > 0 && lastLog[lastLog.length() - 1] != '\n') {
			int nextNewline = msg.indexOf('\n');

			lastMessageLineEnded = nextNewline != -1;

			if (nextNewline == -1) {
				lastLog += msg;
				return;
			} else {
				String remainder = msg.substring(0, nextNewline + 1);
				lastLog += remainder;
				logs.push_back(remainder);
				startPos = nextNewline + 1;
			}
		}
	}

	while (startPos < msg.length()) {
		int nextNewline = msg.indexOf('\n', startPos);
		String lineSegment;

		lastMessageLineEnded = nextNewline != -1;

		if (nextNewline == -1) {
			lineSegment = msg.substring(startPos);
			startPos = msg.length(); // breaks the while loop later on

		} else {
			lineSegment = msg.substring(startPos, nextNewline + 1);
			startPos = nextNewline + 1;
		}

		if (logHistory.size() < MAX_LOG_HISTORY) {
			logHistory.push_back(lineSegment);
		} else {
			logHistory[historyIndex] = lineSegment;
			historyIndex = (historyIndex + 1) % MAX_LOG_HISTORY;
			historyWrapped = true;
		}

        logs.push_back(lineSegment);
	}

    if (logs.size() > 1) {
        broadcastToWebSocket(logs);
    } else if (logs.size() == 1) {
        broadcastToWebSocket(logs[0]);
    }
}

SerialProxy WirelessSerial;