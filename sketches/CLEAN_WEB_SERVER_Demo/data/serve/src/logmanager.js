class LogManager {
	consoleContainer;
	streamUI;
	logs = []

	constructor() {
		this.consoleContainer = document.getElementById("console-container");
		if (!this.consoleContainer) return;

		this.streamUI = this.consoleContainer.querySelector("std-stream");
		if (!this.streamUI) return;
	}

	addline(message) {
		if (!this.streamUI) return;

		const fragment = document.createDocumentFragment();
		const paragraph = document.createElement("p");
		fragment.appendChild(paragraph);
		paragraph.classList.add("std-stream-line");
		paragraph.textContent = message;

		this.streamUI.appendChild(fragment);
	}

	log(message) {
		if (message === null) return;

		if (typeof message === "string") {
			logs.push(message);

			addline(message);
		} else if (
			typeof message === "object" &&
			!Array.isArray(message) &&
			Object.values(message).every((item) => typeof item === "string")
		) {
			this.logs.push(...message);

			for (let k = 0; k < message.length; k++) {
				addLine(message[k]);
			}
		}
	}

	latestlog() {
		return this.logs.at(-1);
	}

	flush() {
		this.logs = [];
	}
}

export function then(resolve) {
	resolve(LogManager);
}
