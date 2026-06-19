const dc = document.getElementsByClassName("dashboard-container").item(0);
const at = document.getElementById("alt-text");

window.addEventListener("hashchange", handleClientsideRouting);
window.addEventListener("DOMContentLoaded", () =>
	handleClientsideRouting(true),
);

const earthImport = import("/src/earth.js");
const socketImport = import("/src/socket.js");

const [Viewer, Socket] = await Promise.all([earthImport, socketImport]);

at.classList.add("hidden");
if (document.location.hash === "") dc.classList.remove("hidden");

const ws = new Socket();

String.prototype.capitalize = function () {
	if (!this) return "";
	return this.at(0).toUpperCase().concat(this.slice(1));
};

function generateFlightPath(flightData) {
	for (let i = 0; i < flightData.length; i++) {
		const dataPoint = flightData[i];

		const dateOfOverflight = new Date(dataPoint.epoch * 1000 || 0);
		const options = {
			weekday: "long",
			year: "numeric",
			month: "long",
			day: "numeric",
			hour: "numeric",
			minute: "numeric",
			second: "numeric",
		};

		Viewer.entities.add({
			description: `
                <p>
                    <b>Location:</b> latitude ${dataPoint.longitude}, longitude ${dataPoint.latitude}, altitude ${dataPoint.height}m<br>
                    <b>Epoch (UNIX time):</b> ${Math.floor(dateOfOverflight.getTime() / 1000)}<br>
                    <b>Date:</b> ${dateOfOverflight.toLocaleDateString(undefined, options).capitalize()}
                </p>`,
			// position: Cesium.Cartesian3.fromDegrees(
			//	dataPoint.longitude,
			//	dataPoint.latitude,
			//	dataPoint.height,
			//),
			position: Cesium.Cartesian3.fromDegrees(
				dataPoint.longitude,
				dataPoint.latitude,
			),
			point: {
				pixelSize: 10,
				color: Cesium.Color.fromRandom(),
				heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
			},
		});
	}
}

let initialGPSdataLoaded = false;
ws.callback = (type, obj) => {
	switch (type) {
		case "SENSORS-HISTORY":
			if (initialGPSdataLoaded) return;
			initialGPSdataLoaded = true;
			let flightData = [];

			console.log("Adding the initial chunk of geopoints");

			for (const point of obj.sensors.gps.pastPoints) {
				flightData.push({
					latitude: point.lat,
					longitude: point.lon,
					height: point.alt,
					epoch: point.epoch,
				});
			}

			generateFlightPath(flightData);

			break;
		case "SENSORS":
			console.log("Adding individual geopoint");

			generateFlightPath([
				{
					latitude: obj.sensors.gps.lat,
					longitude: obj.sensors.gps.lon,
					height: obj.sensors.gps.alt,
					epoch: obj.sensors.gps.epoch,
				},
			]);

			break;
		default:
	}
};

function handleClientsideRouting(firstConnection = false) {
	let currentHash = window.location.hash;

	if (currentHash === "") return;
	if (currentHash === "#earth") currentHash = "#cesium";

	const targetViewId = currentHash.replace("#", "") + "-container";
	const targetViewElement = document.getElementById(targetViewId);

	if (targetViewElement) {
		if (firstConnection) {
			targetViewElement.classList.add("hidden");
			targetViewElement.style["transition"] = "opacity 1s ease-in-out";

			let intervalId = setInterval(() => {
				if (!at.classList.contains("hidden")) return;
				targetViewElement.classList.remove("hidden");
				clearInterval(intervalId);
			}, 1e2);
		}

		targetViewElement.classList.add("active-view");

		document.querySelectorAll(".spa-view").forEach((view) => {
			if (view.id === targetViewId) return;
			view.classList.remove("active-view");
		});

		updateNavigationLinks(currentHash);
	} else {
		console.warn(`Route target ${targetViewId} not found!`);
	}
}

function updateNavigationLinks(activeHash) {
	document.querySelectorAll("nav .nav-card").forEach((link) => {
		const linkHash = link.getAttribute("href");

		if (activeHash === "#cesium") activeHash = "#earth";

		if (linkHash === activeHash) {
			link.classList.add("active");
		} else {
			link.classList.remove("active");
		}
	});
}
