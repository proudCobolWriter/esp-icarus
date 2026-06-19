class Socket {
	// all in second(s)
	static TIMEOUT_LIMIT = 10; // the time after the last packet was received before we start pinging
	static PING_INTERVAL = 5;
	static MAX_PINGS = 3;
	static RETRY_RECONNECT_INTERVAL = 5;

	lastHeartbeatTimestamp = Date.now();
	attempts = 0;

	interval = null;
	callback = null;

	constructor(ip = window.location.hostname) {
		console.log("test interval: ", this.interval);

		this.gateway = `ws://${ip}/ws`;
		this.initWebSocket();
	}

	send(cmd) {
		if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
		this.ws.send(cmd);
	}

	initWebSocket() {
		console.log("Attempting WebSocket connection...");
		this.ws = new WebSocket(this.gateway);
		this.ws.onopen = (event) => this.onOpen(event);
		this.ws.onclose = (event) => this.onClose(event);
		this.ws.onmessage = (event) => this.onMessage(event);
	}

	onOpen(event) {
		console.log("Connection opened successfully!");
		this.send("retrieveReadings");
		this.send("retrieveAllLogs");

		console.log("Started the heartbeat monitor.");
		this.interval = setInterval(
			() => this.onInterval(),
			Socket.PING_INTERVAL * 1000,
		);
	}

	onClose(event) {
		if (this.interval) {
			console.log("Closing the heartbeat monitor.");
			clearInterval(this.interval);
			this.interval = null;
		}

		console.log(
			`Connection closed, retrying connection in ${Socket.RETRY_RECONNECT_INTERVAL}...`,
		);
		setTimeout(
			() => this.initWebSocket(),
			Socket.RETRY_RECONNECT_INTERVAL * 1000,
		);
	}

	onMessage(event) {
		const rawMessage = event.data;

		console.log("Data received:", rawMessage);

		this.lastHeartbeatTimestamp = Date.now();
		this.attempts = 0;

		try {
			let separatorIndex = rawMessage.indexOf(":");
			let type = rawMessage.slice(0, separatorIndex);
			let payload = rawMessage.slice(separatorIndex + 1);

			switch (type) {
				case "PING":
					this.send("PONG");
					break;
				case "PONG":
					console.log("Heartbeat verified.");
					return;
				case "LOG":
					break;
				default:
					payload = JSON.parse(payload || "{}");
			}

			if (!this.callback) return;
			this.callback(type, payload);
		} catch (err) {
			console.error("Error parsing telemetry JSON:", err);
		}
	}

	onInterval() {
		const secondsElapsed =
			(Date.now() - this.lastHeartbeatTimestamp) / 1000;

		if (secondsElapsed >= Socket.TIMEOUT_LIMIT) {
			console.log(
				`No data for over ${Socket.TIMEOUT_LIMIT} seconds... Testing line with PING...`,
			);

			this.send("PING");
			this.attempts += 1;

			if (this.attempts <= Socket.MAX_PINGS) return;

			console.warn(
				"Max pings exceeded without response. Terminating connection.",
			);

			this.ws.close();
		}
	}
}

export function then(resolve) {
	resolve(Socket);
}
