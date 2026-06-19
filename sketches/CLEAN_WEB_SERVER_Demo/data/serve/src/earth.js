// Cesium.Ion.defaultAccessToken = "";

const viewer = new Cesium.Viewer("cesium-container", {
	// baseLayer: Cesium.ImageryLayer.fromWorldImagery({
	//     style: Cesium.IonWorldImageryStyle.AERIAL_WITH_LABELS,
	// }),
	baseLayerPicker: true,
	selectedImageryProviderViewModel:
		Cesium.createDefaultImageryProviderViewModels().find(
			(m) => m.name === "Bing Maps Aerial with Labels",
		),
	terrain: Cesium.Terrain.fromWorldTerrain(),
	sceneModePicker: false,
	timeline: false,
	animation: false,
	shadows: true,
	skyBox: false,
});

document.querySelector(".cesium-viewer-bottom").style.opacity = 0;

if (viewer.infoBox && viewer.infoBox.frame) {
	const iFrame = viewer.infoBox.frame;
	iFrame.setAttribute(
		"sandbox",
		"allow-same-origin allow-scripts allow-popups allow-forms",
	);

	if (iFrame.src === "about:blank") {
		iFrame.src = "javascript:void(0)";
	}
} // or just disable the infoBox

viewer.scene.globe.enableLighting = true;
viewer.clock.shouldAnimate = true;

viewer.scene.highDynamicRange = true;
viewer.scene.postProcessStages.fxaa.enabled = true;
viewer.scene.backgroundColor = Cesium.Color.BLACK;
viewer.resolutionScale = window.devicePixelRatio;

const controller = viewer.scene.screenSpaceCameraController;

controller.enableTilt = false;
controller.maximumTiltAngle = 0.0;

const initializeEnvironment = async () => {
	try {
		console.log("Fetching imagery provider...");

		const imageryProvider =
			await Cesium.IonImageryProvider.fromAssetId(3812);

		const blackMarble =
			Cesium.ImageryLayer.fromProviderAsync(imageryProvider);
		blackMarble.dayAlpha = 0.0;
		blackMarble.nightAlpha = 1.0;
		viewer.imageryLayers.add(blackMarble);

		console.log("Imagery layer successfully applied.");

		console.log("Fetching OSM Buildings...");

		const buildingTileset = await Cesium.createOsmBuildingsAsync();

		viewer.scene.primitives.add(buildingTileset);

		console.log("OSM Buildings successfully added to scene.");
	} catch (error) {
		// This catches errors from BOTH the imagery asset and the OSM buildings
		console.error(`Initialization failure: ${error}`);
	}
};

await initializeEnvironment();

viewer.camera.flyTo({
	destination: Cesium.Cartesian3.fromDegrees(0.0, 30.0, 6e6),
	orientation: {
		heading: Cesium.Math.toRadians(0.0),
		pitch: Cesium.Math.toRadians(-75.0),
	},
});

const allTime = new Cesium.TimeInterval({
	start: Cesium.JulianDate.fromIso8601("1900-01-01T00:00:00Z"),
	stop: Cesium.JulianDate.fromIso8601("2100-01-01T00:00:00Z"),
});

const orbitAltitude = 400e3; // in meters

const orbitPositions = [
	[0, 90, orbitAltitude],
	[0, 0, orbitAltitude],
	[0, -90, orbitAltitude],
	[180, 0, orbitAltitude],
	[0, 90, orbitAltitude],
];

const satelliteOrbitEntity = viewer.entities.add({
	polyline: {
		positions: Cesium.Cartesian3.fromDegreesArrayHeights(
			orbitPositions.flat(),
		),
		material: new Cesium.PolylineGlowMaterialProperty({
			glowPower: 0.5,
			color: Cesium.Color.CYAN,
		}),
		width: 2,
		arcType: Cesium.ArcType.GEODESIC,
	},
});

const orbitDuration = 3600 * 1.5; // 90 minutes sample orbit

const position = new Cesium.SampledPositionProperty();

position.setInterpolationOptions({
	interpolationDegree: 5,
	interpolationAlgorithm: Cesium.LagrangePolynomialApproximation,
});

let orbitStartTime = viewer.clock.currentTime.clone();

function refreshOrbitData(startTime) {
	position.removeSamples(allTime);

	for (let i = 0; i <= 360; i += 360 / 100) {
		const time = Cesium.JulianDate.addSeconds(
			startTime,
			(i / 360) * orbitDuration,
			new Cesium.JulianDate(),
		);
		const pos = Cesium.Cartesian3.fromDegrees(i, 0, orbitAltitude);
		position.addSample(time, pos);
	}
}

refreshOrbitData(orbitStartTime);

viewer.clock.onTick.addEventListener((clock) => {
	const secondsElapsed = Cesium.JulianDate.secondsDifference(
		clock.currentTime,
		orbitStartTime,
	);

	if (secondsElapsed >= orbitDuration || secondsElapsed < 0) {
		orbitStartTime = clock.currentTime.clone();
		refreshOrbitData(orbitStartTime);
	}
});

const satelliteEntity = viewer.entities.add({
	position,
	orientation: Cesium.VelocityOrientationProperty(position),
	billboard: {
		image: "https://cdn-icons-png.flaticon.com/512/1042/1042820.png",
		width: 32,
		height: 32,
	},
	id: "Sat01",
	name: "GPS Satellite",
	label: {
		text: "GPS Satellite",
		font: "14pt monospace",
		scale: 1.0,
		style: Cesium.LabelStyle.FILL,
		fillColor: Cesium.Color.WHITE,
		verticalOrigin: Cesium.VerticalOrigin.TOP,
		pixelOffset: new Cesium.Cartesian2(0, 32),
		showBackground: true,
		backgroundColor: new Cesium.Color(0, 0, 0, 0),
		backgroundPadding: new Cesium.Cartesian2(0, 0),
	},
	path: {
		resolution: 1,
		material: new Cesium.StripeMaterialProperty({
			evenColor: Cesium.Color.CYAN.withAlpha(1),
			oddColor: Cesium.Color.CYAN.withAlpha(0.0),
			repeat: 1,
			offset: 0.5,
			orientation: Cesium.StripeOrientation.VERTICAL,
		}),
		width: 4,
		leadTime: 0,
		trailTime: orbitDuration,
	},
});

async function loadModel(asset) {
	let modelUri;

	try {
		modelUri =
			typeof asset === "number"
				? await Cesium.IonResource.fromAssetId(asset)
				: asset;
	} catch (err) {
		throw new Error(`Failed to resolve asset URI: ${err.message}`);
	}

	const entity = viewer.entities.add({
		//availability: new Cesium.TimeIntervalCollection([
		//	new Cesium.TimeInterval({ start: start, stop: stop }),
		//]),
		//position: positionProperty,
		//orientation: new Cesium.VelocityOrientationProperty(
		//	positionProperty,
		//),
		position: Cesium.Cartesian3.fromDegrees(0, 0),
		model: {
			uri: modelUri,
			minimumPixelSize: 128,
			maximumScale: 100e3,
		},
		path: new Cesium.PathGraphics({ width: 3 }),
	});

	return entity;
}

loadModel("/models/satellite.glb")
	.then((entity) => {
		console.log("Model asset ready in memory: ", entity);
	})
	.catch((error) => {
		console.error("Error loading model asset: ", error);
	});

viewer.scene.postRender.addEventListener(function onFirstRender() {
	const container = document.getElementById("cesium-container");
	container.style.visibility = "visible";
	viewer.scene.postRender.removeEventListener(onFirstRender);
	console.log("First frame fully drawn.");
});

export function then(resolve) {
	resolve(viewer);
}
