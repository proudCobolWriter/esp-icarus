#ifndef SERIAL_PROXY_H
#define SERIAL_PROXY_H

#include <Arduino.h>
#include <vector>

String getFormattedTime();

void broadcastToWebSocket(const String &msg);
void addLogToHistory(const String &msg);

// CONSTANTS

extern const size_t MAX_LOG_HISTORY;

// VARIABLES

extern std::vector<String> logHistory;
extern size_t historyIndex;
extern bool historyWrapped;
extern bool lastMessageLineEnded; // used for print prefixes (datetime)

class SerialProxy {
  public:
	void begin(unsigned long baud) { Serial0.begin(baud); }

	operator bool() { return (bool)Serial0; }

	template <typename T> size_t print(const T &val) {
		String rawStr = String(val);
		String logStr = rawStr;

		size_t bytesWritten = 0;
		if (lastMessageLineEnded && rawStr.length() > 0) {
			String ts = getFormattedTime() + " ";
			bytesWritten += Serial0.print(ts);
			logStr = ts + rawStr; // prepend timestamp...
		}
		bytesWritten += Serial0.print(rawStr);

		addLogToHistory(logStr);
		return bytesWritten;
	}
	size_t print(double val, int digits) {
		String rawStr = String(val, digits);
		String logStr = rawStr;

		size_t bytesWritten = 0;
		if (lastMessageLineEnded) {
			String ts = getFormattedTime() + " ";
			bytesWritten += Serial0.print(ts);
			logStr = ts + rawStr;
		}
		bytesWritten += Serial0.print(rawStr);

		addLogToHistory(logStr);
		return bytesWritten;
	}

	template <typename T> size_t println(const T &val) {
		String rawStr = String(val) + "\n";
		String logStr = rawStr;

		size_t bytesWritten = 0;
		if (lastMessageLineEnded && rawStr.length() > 0) {
			String ts = getFormattedTime() + " ";
			bytesWritten += Serial0.print(ts);
			logStr = ts + rawStr;
		}
		bytesWritten += Serial0.print(rawStr);

		addLogToHistory(logStr);
		return bytesWritten;
	}
	size_t println() {
		addLogToHistory("\n");
		return Serial0.println();
	}
	size_t println(double val, int digits) {
		String rawStr = String(val, digits) + "\n";
		String logStr = rawStr;

		size_t bytesWritten = 0;
		if (lastMessageLineEnded) {
			String ts = getFormattedTime() + " ";
			bytesWritten += Serial0.print(ts);
			logStr = ts + rawStr;
		}
		bytesWritten += Serial0.print(rawStr);

		addLogToHistory(logStr);
		return bytesWritten;
	}

	template <typename... Args> size_t printf(const char *format, Args... args) {
		char loc_buf[128];
		size_t bytesWritten = 0;
		int len = snprintf(loc_buf, sizeof(loc_buf), format, args...);

		if (len >= 0) {
			String rawStr(loc_buf);
			String logStr = rawStr;

			if (lastMessageLineEnded && rawStr.length() > 0) {
				String ts = getFormattedTime() + " ";
				bytesWritten += Serial0.print(ts);
				logStr = ts + rawStr;
			}
			bytesWritten += Serial0.printf(format, args...);

			addLogToHistory(logStr);
		}

		return bytesWritten;
	};
};

extern SerialProxy WirelessSerial;

#undef Serial
#define Serial WirelessSerial
#endif