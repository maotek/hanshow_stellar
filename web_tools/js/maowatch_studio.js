"use strict";

const SCREEN_SIZE = 200;
const PLANE_SIZE = SCREEN_SIZE * SCREEN_SIZE / 8;
const SERVICE_UUID = "00001f10-0000-1000-8000-00805f9b34fb";
const CHARACTERISTIC_UUID = "00001f1f-0000-1000-8000-00805f9b34fb";
const EPD_SERVICE_UUID = "13187b10-eba9-a3ba-044e-83d3217d9a38";
const EPD_CHARACTERISTIC_UUID = "4b646063-6264-f3a7-8941-e65356ea82fe";
const OTA_SERVICE_UUID = "0000221f-0000-1000-8000-00805f9b34fb";
const OTA_CHARACTERISTIC_UUID = "0000331f-0000-1000-8000-00805f9b34fb";
const OTA_BANK_START = 0x20000;
const OTA_MAX_SIZE = 0x20000;

const $ = selector => document.querySelector(selector);
const connectionEl = $("#connection");
const connectButton = $("#connect");
const disconnectButton = $("#disconnect");
const syncButton = $("#sync");
const sendImageButton = $("#send-image");
const showWatchButton = $("#show-watch");
const imageProgress = $("#image-progress");
const firmwareFile = $("#firmware-file");
const uploadButton = $("#upload");
const otaProgress = $("#ota-progress");
const clockEl = $("#clock");
const dateEl = $("#date");
const logEl = $("#log");

const imageFile = $("#image-file");
const dropzone = $("#dropzone");
const cropStage = $("#crop-stage");
const cropCanvas = $("#crop-canvas");
const cropContext = cropCanvas.getContext("2d", { alpha: false });
const resetCropButton = $("#reset-crop");
const rotateLeftButton = $("#rotate-left");
const rotateRightButton = $("#rotate-right");
const flipImageButton = $("#flip-image");
const resetLookButton = $("#reset-look");
const screenCanvas = $("#screen-canvas");
const screenContext = screenCanvas.getContext("2d", { alpha: false });
const paletteControl = $("#palette");
const ditherControl = $("#dither");
const brightnessControl = $("#brightness");
const contrastControl = $("#contrast");
const redBiasControl = $("#red-bias");
const brightnessValue = $("#brightness-value");
const contrastValue = $("#contrast-value");
const redValue = $("#red-value");

const qrText = $("#qr-text");
const qrColor = $("#qr-color");
const qrLevel = $("#qr-level");
const qrSize = $("#qr-size");
const qrSizeValue = $("#qr-size-value");
const makeQrButton = $("#make-qr");
const removeQrButton = $("#remove-qr");
const centerQrButton = $("#center-qr");
const canvasTool = $("#canvas-tool");
const brushColor = $("#brush-color");
const brushSize = $("#brush-size");
const brushSizeValue = $("#brush-size-value");
const undoDrawingButton = $("#undo-drawing");
const clearDrawingButton = $("#clear-drawing");
const textContent = $("#text-content");
const textColor = $("#text-color");
const textFont = $("#text-font");
const textSize = $("#text-size");
const textSizeValue = $("#text-size-value");
const placeTextButton = $("#place-text");
const removeTextButton = $("#remove-text");

let device;
let timeCharacteristic;
let epdCharacteristic;
let otaCharacteristic;
let selectedFirmware;
let otaNotificationResolver;
let uploadingFirmware = false;
let uploadingImage = false;

let sourceImage;
let sourceName = "";
let imageRotation = 0;
let imageFlipped = false;
let crop = { x: 0, y: 0, size: 1 };
let cropGesture;
let renderQueued = false;
let qrLayer;
let qrGesture;
let textLayer;
let textGesture;
let drawingGesture;
let drawingStrokes = [];

const workingCanvas = document.createElement("canvas");
workingCanvas.width = SCREEN_SIZE;
workingCanvas.height = SCREEN_SIZE;
const workingContext = workingCanvas.getContext("2d", { alpha: false });
const drawingCanvas = document.createElement("canvas");
drawingCanvas.width = SCREEN_SIZE;
drawingCanvas.height = SCREEN_SIZE;
const drawingContext = drawingCanvas.getContext("2d");

function log(message, isError = false) {
  logEl.textContent = message;
  logEl.classList.toggle("error", isError);
}

function updateClock() {
  const now = new Date();
  clockEl.textContent = new Intl.DateTimeFormat([], {
    hour: "2-digit",
    minute: "2-digit",
    hour12: false
  }).format(now);
  dateEl.textContent = new Intl.DateTimeFormat([], {
    weekday: "long",
    year: "numeric",
    month: "long",
    day: "numeric"
  }).format(now);
}

function setConnected(connected) {
  connectionEl.textContent = connected ? "CONNECTED" : "DISCONNECTED";
  connectionEl.classList.toggle("connected", connected);
  syncButton.disabled = !connected || uploadingFirmware || uploadingImage;
  disconnectButton.disabled = !connected || uploadingFirmware || uploadingImage;
  uploadButton.disabled = !connected || !selectedFirmware || uploadingFirmware || uploadingImage;
  sendImageButton.disabled = !connected || !epdCharacteristic || uploadingFirmware || uploadingImage;
  showWatchButton.disabled = !connected || !timeCharacteristic || uploadingFirmware || uploadingImage;
  connectButton.textContent = connected ? "Reconnect" : "Connect";
}

function onDisconnected() {
  timeCharacteristic = undefined;
  epdCharacteristic = undefined;
  otaCharacteristic = undefined;
  setConnected(false);
  if (!uploadingFirmware && !uploadingImage) log("Watch disconnected.");
}

async function connect() {
  if (!navigator.bluetooth) {
    throw new Error("Web Bluetooth is unavailable. On iPhone, open this page in Bluefy.");
  }

  if (device?.gatt?.connected) device.gatt.disconnect();
  log("Choose MAOWATCH in the Bluetooth picker…");
  device = await navigator.bluetooth.requestDevice({
    acceptAllDevices: true,
    optionalServices: [SERVICE_UUID, EPD_SERVICE_UUID, OTA_SERVICE_UUID]
  });

  device.addEventListener("gattserverdisconnected", onDisconnected);
  const server = await device.gatt.connect();

  const [timeService, epdService, otaService] = await Promise.all([
    server.getPrimaryService(SERVICE_UUID),
    server.getPrimaryService(EPD_SERVICE_UUID),
    server.getPrimaryService(OTA_SERVICE_UUID)
  ]);

  [timeCharacteristic, epdCharacteristic, otaCharacteristic] = await Promise.all([
    timeService.getCharacteristic(CHARACTERISTIC_UUID),
    epdService.getCharacteristic(EPD_CHARACTERISTIC_UUID),
    otaService.getCharacteristic(OTA_CHARACTERISTIC_UUID)
  ]);

  await otaCharacteristic.startNotifications();
  otaCharacteristic.addEventListener("characteristicvaluechanged", event => {
    if (!otaNotificationResolver) return;
    otaNotificationResolver(new Uint8Array(event.target.value.buffer.slice(0)));
    otaNotificationResolver = undefined;
  });

  try {
    await epdCharacteristic.startNotifications();
  } catch (_) {
    // Uploads use writes with response, so notifications are helpful but optional.
  }

  setConnected(true);
  log(`Connected to ${device.name || "MAOWATCH"}. Display studio ready.`);
}

function disconnect() {
  if (device?.gatt?.connected) device.gatt.disconnect();
  else onDisconnected();
}

async function writeGatt(characteristic, bytes) {
  if (!characteristic) throw new Error("The requested watch service is unavailable.");
  if (typeof characteristic.writeValueWithResponse === "function") {
    await characteristic.writeValueWithResponse(bytes);
  } else {
    await characteristic.writeValue(bytes);
  }
}

function makeTimePacket(now = new Date()) {
  const localEpoch = (
    Math.floor(now.getTime() / 1000) - now.getTimezoneOffset() * 60
  ) >>> 0;
  const year = now.getFullYear();
  return new Uint8Array([
    0xdd,
    (localEpoch >>> 24) & 0xff,
    (localEpoch >>> 16) & 0xff,
    (localEpoch >>> 8) & 0xff,
    localEpoch & 0xff,
    (year >>> 8) & 0xff,
    year & 0xff,
    now.getMonth() + 1,
    now.getDate(),
    now.getDay()
  ]);
}

function toHex(bytes) {
  return [...bytes].map(value => value.toString(16).padStart(2, "0")).join(" ").toUpperCase();
}

async function syncTime() {
  const packet = makeTimePacket();
  syncButton.disabled = true;
  log(`Sending ${toHex(packet)}…`);
  await writeGatt(timeCharacteristic, packet);
  await writeGatt(timeCharacteristic, new Uint8Array([0xe2]));
  updateClock();
  setConnected(true);
  log(`Time synchronized · ${toHex(packet)}`);
}

function pointInCanvas(event, canvas) {
  const rect = canvas.getBoundingClientRect();
  return {
    x: (event.clientX - rect.left) * canvas.width / rect.width,
    y: (event.clientY - rect.top) * canvas.height / rect.height
  };
}

function resetCrop() {
  if (!sourceImage) return;
  const size = Math.min(sourceImage.naturalWidth, sourceImage.naturalHeight);
  crop = {
    x: (sourceImage.naturalWidth - size) / 2,
    y: (sourceImage.naturalHeight - size) / 2,
    size
  };
  drawCropEditor();
  queueRender();
}

function setupCropCanvas() {
  if (!sourceImage) return;
  const maxWidth = 640;
  const maxHeight = 460;
  const scale = Math.min(maxWidth / sourceImage.naturalWidth, maxHeight / sourceImage.naturalHeight, 1);
  cropCanvas.width = Math.max(1, Math.round(sourceImage.naturalWidth * scale));
  cropCanvas.height = Math.max(1, Math.round(sourceImage.naturalHeight * scale));
  cropStage.classList.add("visible");
  resetCropButton.disabled = false;
  rotateLeftButton.disabled = false;
  rotateRightButton.disabled = false;
  flipImageButton.disabled = false;
  resetLookButton.disabled = false;
  resetCrop();
}

function cropScale() {
  return {
    x: cropCanvas.width / sourceImage.naturalWidth,
    y: cropCanvas.height / sourceImage.naturalHeight
  };
}

function drawCropEditor() {
  if (!sourceImage) return;
  cropContext.clearRect(0, 0, cropCanvas.width, cropCanvas.height);
  cropContext.drawImage(sourceImage, 0, 0, cropCanvas.width, cropCanvas.height);

  const scale = cropScale();
  const x = crop.x * scale.x;
  const y = crop.y * scale.y;
  const width = crop.size * scale.x;
  const height = crop.size * scale.y;

  cropContext.save();
  cropContext.fillStyle = "rgba(8, 9, 8, .58)";
  cropContext.beginPath();
  cropContext.rect(0, 0, cropCanvas.width, cropCanvas.height);
  cropContext.rect(x, y, width, height);
  cropContext.fill("evenodd");
  cropContext.strokeStyle = "#d8ff45";
  cropContext.lineWidth = 2;
  cropContext.strokeRect(x + 1, y + 1, width - 2, height - 2);

  const handle = Math.max(7, Math.min(13, width / 8));
  cropContext.fillStyle = "#d8ff45";
  for (const [hx, hy] of [[x, y], [x + width, y], [x, y + height], [x + width, y + height]]) {
    cropContext.fillRect(hx - handle / 2, hy - handle / 2, handle, handle);
    cropContext.strokeStyle = "#171815";
    cropContext.lineWidth = 1;
    cropContext.strokeRect(hx - handle / 2, hy - handle / 2, handle, handle);
  }
  cropContext.restore();
}

function cropHitTest(point) {
  const scale = cropScale();
  const bounds = {
    x: crop.x * scale.x,
    y: crop.y * scale.y,
    sizeX: crop.size * scale.x,
    sizeY: crop.size * scale.y
  };
  const hit = 16;
  const corners = {
    nw: [bounds.x, bounds.y],
    ne: [bounds.x + bounds.sizeX, bounds.y],
    sw: [bounds.x, bounds.y + bounds.sizeY],
    se: [bounds.x + bounds.sizeX, bounds.y + bounds.sizeY]
  };
  for (const [name, [x, y]] of Object.entries(corners)) {
    if (Math.hypot(point.x - x, point.y - y) <= hit) return name;
  }
  if (
    point.x >= bounds.x && point.x <= bounds.x + bounds.sizeX &&
    point.y >= bounds.y && point.y <= bounds.y + bounds.sizeY
  ) return "move";
  return "new";
}

function startCropGesture(event) {
  if (!sourceImage) return;
  cropCanvas.setPointerCapture(event.pointerId);
  const point = pointInCanvas(event, cropCanvas);
  const scale = cropScale();
  cropGesture = {
    mode: cropHitTest(point),
    startX: point.x / scale.x,
    startY: point.y / scale.y,
    original: { ...crop }
  };
}

function updateCropGesture(event) {
  if (!cropGesture || !sourceImage) return;
  const point = pointInCanvas(event, cropCanvas);
  const scale = cropScale();
  const px = Math.max(0, Math.min(sourceImage.naturalWidth, point.x / scale.x));
  const py = Math.max(0, Math.min(sourceImage.naturalHeight, point.y / scale.y));
  const original = cropGesture.original;

  if (cropGesture.mode === "move") {
    crop.x = Math.max(0, Math.min(sourceImage.naturalWidth - crop.size,
      original.x + px - cropGesture.startX));
    crop.y = Math.max(0, Math.min(sourceImage.naturalHeight - crop.size,
      original.y + py - cropGesture.startY));
  } else if (cropGesture.mode === "new") {
    const size = Math.max(16, Math.min(
      Math.abs(px - cropGesture.startX),
      Math.abs(py - cropGesture.startY)
    ));
    crop.x = Math.min(px, cropGesture.startX);
    crop.y = Math.min(py, cropGesture.startY);
    crop.size = Math.min(size, sourceImage.naturalWidth - crop.x, sourceImage.naturalHeight - crop.y);
  } else {
    const right = original.x + original.size;
    const bottom = original.y + original.size;
    let anchorX = cropGesture.mode.includes("w") ? right : original.x;
    let anchorY = cropGesture.mode.includes("n") ? bottom : original.y;
    let size = Math.max(16, Math.max(Math.abs(px - anchorX), Math.abs(py - anchorY)));
    size = Math.min(size,
      cropGesture.mode.includes("w") ? anchorX : sourceImage.naturalWidth - anchorX,
      cropGesture.mode.includes("n") ? anchorY : sourceImage.naturalHeight - anchorY
    );
    crop.x = cropGesture.mode.includes("w") ? anchorX - size : anchorX;
    crop.y = cropGesture.mode.includes("n") ? anchorY - size : anchorY;
    crop.size = size;
  }

  drawCropEditor();
  queueRender();
}

function endCropGesture(event) {
  if (cropCanvas.hasPointerCapture(event.pointerId)) cropCanvas.releasePointerCapture(event.pointerId);
  cropGesture = undefined;
}

async function loadImageFile(file) {
  if (!file || !file.type.startsWith("image/")) throw new Error("Choose a PNG, JPEG, WebP, GIF or BMP image.");
  const url = URL.createObjectURL(file);
  const image = new Image();
  try {
    await new Promise((resolve, reject) => {
      image.onload = resolve;
      image.onerror = () => reject(new Error("The selected image could not be decoded."));
      image.src = url;
    });
    sourceImage = image;
    sourceName = file.name;
    imageRotation = 0;
    imageFlipped = false;
    setupCropCanvas();
    queueRender();
    log(`Loaded ${file.name} · ${image.naturalWidth}×${image.naturalHeight}. Adjust the crop in place.`);
  } finally {
    URL.revokeObjectURL(url);
  }
}

function clampChannel(value) {
  return Math.max(0, Math.min(255, value));
}

function colorDistance(r, g, b, target, redBias) {
  const dr = r - target[0];
  const dg = g - target[1];
  const db = b - target[2];
  let distance = dr * dr * 0.9 + dg * dg * 1.25 + db * db * 0.75;
  if (target[0] === 225 && target[1] === 38) distance -= redBias * 520;
  return distance;
}

function nearestColor(r, g, b, palette, redBias) {
  let best = palette[0];
  let bestDistance = Infinity;
  for (const color of palette) {
    const distance = colorDistance(r, g, b, color, redBias);
    if (distance < bestDistance) {
      best = color;
      bestDistance = distance;
    }
  }
  return best;
}

function quantizeCanvas(canvas, mode, algorithm, redBias) {
  const context = canvas.getContext("2d", { alpha: false });
  const image = context.getImageData(0, 0, canvas.width, canvas.height);
  const data = image.data;
  const width = canvas.width;
  const height = canvas.height;
  const palette = mode === "bwr"
    ? [[0, 0, 0], [255, 255, 255], [225, 38, 28]]
    : [[0, 0, 0], [255, 255, 255]];
  const values = new Float32Array(width * height * 3);
  for (let pixel = 0; pixel < width * height; pixel++) {
    values[pixel * 3] = data[pixel * 4];
    values[pixel * 3 + 1] = data[pixel * 4 + 1];
    values[pixel * 3 + 2] = data[pixel * 4 + 2];
  }

  const bayer = [
    [0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]
  ];
  const spread = algorithm === "floyd"
    ? [[1, 0, 7 / 16], [-1, 1, 3 / 16], [0, 1, 5 / 16], [1, 1, 1 / 16]]
    : [[1, 0, 1 / 8], [2, 0, 1 / 8], [-1, 1, 1 / 8], [0, 1, 1 / 8], [1, 1, 1 / 8], [0, 2, 1 / 8]];

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const pixel = y * width + x;
      const index = pixel * 3;
      let r = values[index];
      let g = values[index + 1];
      let b = values[index + 2];
      if (algorithm === "bayer") {
        const adjustment = (bayer[y & 3][x & 3] - 7.5) * 7;
        r += adjustment;
        g += adjustment;
        b += adjustment;
      }
      const color = nearestColor(r, g, b, palette, redBias);
      data[pixel * 4] = color[0];
      data[pixel * 4 + 1] = color[1];
      data[pixel * 4 + 2] = color[2];
      data[pixel * 4 + 3] = 255;

      if (algorithm === "floyd" || algorithm === "atkinson") {
        const error = [r - color[0], g - color[1], b - color[2]];
        for (const [dx, dy, weight] of spread) {
          const nx = x + dx;
          const ny = y + dy;
          if (nx < 0 || nx >= width || ny >= height) continue;
          const neighbor = (ny * width + nx) * 3;
          values[neighbor] += error[0] * weight;
          values[neighbor + 1] += error[1] * weight;
          values[neighbor + 2] += error[2] * weight;
        }
      }
    }
  }
  context.putImageData(image, 0, 0);
}

function applyTone(context) {
  const brightness = Number(brightnessControl.value);
  const contrast = Number(contrastControl.value);
  const image = context.getImageData(0, 0, SCREEN_SIZE, SCREEN_SIZE);
  const factor = (259 * (contrast + 255)) / (255 * (259 - contrast));
  for (let index = 0; index < image.data.length; index += 4) {
    image.data[index] = clampChannel(factor * (image.data[index] - 128) + 128 + brightness);
    image.data[index + 1] = clampChannel(factor * (image.data[index + 1] - 128) + 128 + brightness);
    image.data[index + 2] = clampChannel(factor * (image.data[index + 2] - 128) + 128 + brightness);
  }
  context.putImageData(image, 0, 0);
}

function createQrLayer() {
  const text = qrText.value.trim();
  if (!text) throw new Error("Enter text or a URL for the QR code.");
  if (typeof window.qrcode !== "function") {
    throw new Error("The QR library did not load. Check the internet connection and reload the page.");
  }

  const code = window.qrcode(0, qrLevel.value);
  code.addData(text);
  code.make();
  const modules = code.getModuleCount();
  const desired = Number(qrSize.value);
  const quietModules = 1;
  const moduleSize = Math.max(1, Math.floor(desired / (modules + quietModules * 2)));
  const actualSize = (modules + quietModules * 2) * moduleSize;
  const previousCenter = qrLayer
    ? { x: qrLayer.x + qrLayer.size / 2, y: qrLayer.y + qrLayer.size / 2 }
    : { x: SCREEN_SIZE / 2, y: SCREEN_SIZE / 2 };

  qrLayer = {
    code,
    modules,
    moduleSize,
    quietModules,
    size: actualSize,
    x: Math.round(previousCenter.x - actualSize / 2),
    y: Math.round(previousCenter.y - actualSize / 2),
    color: qrColor.value
  };
  qrLayer.x = Math.max(0, Math.min(SCREEN_SIZE - qrLayer.size, qrLayer.x));
  qrLayer.y = Math.max(0, Math.min(SCREEN_SIZE - qrLayer.size, qrLayer.y));
  qrSizeValue.value = `${actualSize} px`;
  removeQrButton.disabled = false;
  centerQrButton.disabled = false;
  screenCanvas.classList.add("qr-active");
  queueRender();
}

function drawQr(context) {
  if (!qrLayer) return;
  const { code, modules, moduleSize, quietModules, x, y } = qrLayer;
  context.fillStyle = "#fff";
  context.fillRect(x, y, qrLayer.size, qrLayer.size);
  context.fillStyle = qrLayer.color === "red" ? "#e1261c" : "#000";
  const quiet = quietModules * moduleSize;
  for (let row = 0; row < modules; row++) {
    for (let column = 0; column < modules; column++) {
      if (code.isDark(row, column)) {
        context.fillRect(x + quiet + column * moduleSize, y + quiet + row * moduleSize, moduleSize, moduleSize);
      }
    }
  }
}

function renderDrawingLayer() {
  drawingContext.clearRect(0, 0, SCREEN_SIZE, SCREEN_SIZE);
  for (const stroke of drawingStrokes) {
    if (!stroke.points.length) continue;
    drawingContext.save();
    drawingContext.globalCompositeOperation = stroke.erase ? "destination-out" : "source-over";
    drawingContext.strokeStyle = stroke.color === "red"
      ? "#e1261c"
      : stroke.color === "white" ? "#fff" : "#000";
    drawingContext.fillStyle = drawingContext.strokeStyle;
    drawingContext.lineWidth = stroke.size;
    drawingContext.lineCap = "round";
    drawingContext.lineJoin = "round";
    if (stroke.points.length === 1) {
      drawingContext.beginPath();
      drawingContext.arc(stroke.points[0].x, stroke.points[0].y, stroke.size / 2, 0, Math.PI * 2);
      drawingContext.fill();
    } else {
      drawingContext.beginPath();
      drawingContext.moveTo(stroke.points[0].x, stroke.points[0].y);
      for (let index = 1; index < stroke.points.length; index++) {
        const point = stroke.points[index];
        drawingContext.lineTo(point.x, point.y);
      }
      drawingContext.stroke();
    }
    drawingContext.restore();
  }
  undoDrawingButton.disabled = drawingStrokes.length === 0;
  clearDrawingButton.disabled = drawingStrokes.length === 0;
}

function drawDrawing(context) {
  renderDrawingLayer();
  context.drawImage(drawingCanvas, 0, 0);
}

function textFontValue(layer) {
  const family = layer.font === "sans"
    ? "Arial, sans-serif"
    : layer.font === "serif"
      ? "Georgia, serif"
      : "ui-monospace, Consolas, monospace";
  return `700 ${layer.size}px ${family}`;
}

function measureTextLayer(layer) {
  workingContext.save();
  workingContext.font = textFontValue(layer);
  const metrics = workingContext.measureText(layer.text);
  workingContext.restore();
  layer.width = Math.ceil(metrics.width);
  layer.height = Math.ceil(layer.size * 1.2);
}

function createTextLayer() {
  const text = textContent.value.trim();
  if (!text) throw new Error("Enter some text to place on the canvas.");
  const previousCenter = textLayer
    ? { x: textLayer.x + textLayer.width / 2, y: textLayer.y + textLayer.height / 2 }
    : { x: SCREEN_SIZE / 2, y: SCREEN_SIZE / 2 };
  textLayer = {
    text,
    color: textColor.value,
    font: textFont.value,
    size: Number(textSize.value),
    x: 0,
    y: 0,
    width: 0,
    height: 0
  };
  measureTextLayer(textLayer);
  textLayer.x = Math.round(Math.max(0, Math.min(SCREEN_SIZE - textLayer.width,
    previousCenter.x - textLayer.width / 2)));
  textLayer.y = Math.round(Math.max(0, Math.min(SCREEN_SIZE - textLayer.height,
    previousCenter.y - textLayer.height / 2)));
  removeTextButton.disabled = false;
  screenCanvas.classList.add("qr-active");
  queueRender();
}

function drawText(context) {
  if (!textLayer) return;
  context.save();
  context.font = textFontValue(textLayer);
  context.textAlign = "left";
  context.textBaseline = "top";
  context.fillStyle = textLayer.color === "red"
    ? "#e1261c"
    : textLayer.color === "white" ? "#fff" : "#000";
  context.fillText(textLayer.text, textLayer.x, textLayer.y);
  context.restore();
}

function updateColorStats() {
  const pixels = screenContext.getImageData(0, 0, SCREEN_SIZE, SCREEN_SIZE).data;
  let black = 0;
  let red = 0;
  const total = SCREEN_SIZE * SCREEN_SIZE;
  for (let index = 0; index < pixels.length; index += 4) {
    const r = pixels[index];
    const g = pixels[index + 1];
    const b = pixels[index + 2];
    if (r > 150 && r > g * 1.6 && r > b * 1.6) red++;
    else if (r + g + b < 300) black++;
  }
  const blackPercent = black / total * 100;
  const redPercent = red / total * 100;
  const whitePercent = 100 - blackPercent - redPercent;
  $("#color-stats").textContent =
    `Black ${blackPercent.toFixed(0)}% · White ${whitePercent.toFixed(0)}% · Red ${redPercent.toFixed(0)}%`;
  $("#meter-black").style.width = `${blackPercent}%`;
  $("#meter-white").style.width = `${whitePercent}%`;
  $("#meter-red").style.width = `${redPercent}%`;
}

function renderComposition() {
  renderQueued = false;
  workingContext.fillStyle = "#fff";
  workingContext.fillRect(0, 0, SCREEN_SIZE, SCREEN_SIZE);
  if (sourceImage) {
    workingContext.save();
    workingContext.translate(SCREEN_SIZE / 2, SCREEN_SIZE / 2);
    workingContext.rotate(imageRotation * Math.PI / 180);
    workingContext.scale(imageFlipped ? -1 : 1, 1);
    workingContext.drawImage(
      sourceImage,
      crop.x, crop.y, crop.size, crop.size,
      -SCREEN_SIZE / 2, -SCREEN_SIZE / 2, SCREEN_SIZE, SCREEN_SIZE
    );
    workingContext.restore();
    applyTone(workingContext);
    quantizeCanvas(workingCanvas, paletteControl.value, ditherControl.value, Number(redBiasControl.value));
  }
  drawDrawing(workingContext);
  drawQr(workingContext);
  drawText(workingContext);
  screenContext.clearRect(0, 0, SCREEN_SIZE, SCREEN_SIZE);
  screenContext.drawImage(workingCanvas, 0, 0);
  updateColorStats();
}

function queueRender() {
  if (renderQueued) return;
  renderQueued = true;
  requestAnimationFrame(renderComposition);
}

function screenPointerDown(event) {
  const point = pointInCanvas(event, screenCanvas);
  if (canvasTool.value === "draw" || canvasTool.value === "erase") {
    screenCanvas.setPointerCapture(event.pointerId);
    drawingGesture = {
      erase: canvasTool.value === "erase",
      color: brushColor.value,
      size: Number(brushSize.value),
      points: [{
        x: Math.max(0, Math.min(SCREEN_SIZE, point.x)),
        y: Math.max(0, Math.min(SCREEN_SIZE, point.y))
      }]
    };
    drawingStrokes.push(drawingGesture);
    queueRender();
    return;
  }
  if (
    textLayer &&
    point.x >= textLayer.x && point.x <= textLayer.x + textLayer.width &&
    point.y >= textLayer.y && point.y <= textLayer.y + textLayer.height
  ) {
    screenCanvas.setPointerCapture(event.pointerId);
    screenCanvas.classList.add("qr-dragging");
    textGesture = { offsetX: point.x - textLayer.x, offsetY: point.y - textLayer.y };
    return;
  }
  if (!qrLayer) return;
  if (
    point.x < qrLayer.x || point.x > qrLayer.x + qrLayer.size ||
    point.y < qrLayer.y || point.y > qrLayer.y + qrLayer.size
  ) return;
  screenCanvas.setPointerCapture(event.pointerId);
  screenCanvas.classList.add("qr-dragging");
  qrGesture = { offsetX: point.x - qrLayer.x, offsetY: point.y - qrLayer.y };
}

function screenPointerMove(event) {
  const point = pointInCanvas(event, screenCanvas);
  if (drawingGesture) {
    const next = {
      x: Math.max(0, Math.min(SCREEN_SIZE, point.x)),
      y: Math.max(0, Math.min(SCREEN_SIZE, point.y))
    };
    const previous = drawingGesture.points[drawingGesture.points.length - 1];
    if (Math.hypot(next.x - previous.x, next.y - previous.y) >= .35) {
      drawingGesture.points.push(next);
      queueRender();
    }
    return;
  }
  if (textGesture && textLayer) {
    textLayer.x = Math.round(Math.max(0, Math.min(
      Math.max(0, SCREEN_SIZE - textLayer.width), point.x - textGesture.offsetX)));
    textLayer.y = Math.round(Math.max(0, Math.min(
      Math.max(0, SCREEN_SIZE - textLayer.height), point.y - textGesture.offsetY)));
    queueRender();
    return;
  }
  if (!qrGesture || !qrLayer) return;
  qrLayer.x = Math.round(Math.max(0, Math.min(SCREEN_SIZE - qrLayer.size,
    point.x - qrGesture.offsetX)));
  qrLayer.y = Math.round(Math.max(0, Math.min(SCREEN_SIZE - qrLayer.size,
    point.y - qrGesture.offsetY)));
  queueRender();
}

function screenPointerUp(event) {
  if (screenCanvas.hasPointerCapture(event.pointerId)) screenCanvas.releasePointerCapture(event.pointerId);
  screenCanvas.classList.remove("qr-dragging");
  qrGesture = undefined;
  textGesture = undefined;
  drawingGesture = undefined;
}

function canvasToPlanes(canvas) {
  const pixels = canvas.getContext("2d").getImageData(0, 0, SCREEN_SIZE, SCREEN_SIZE).data;
  const black = new Uint8Array(PLANE_SIZE);
  const red = new Uint8Array(PLANE_SIZE);
  let output = 0;

  for (let x = SCREEN_SIZE - 1; x >= 0; x--) {
    for (let y = 0; y < SCREEN_SIZE; y += 8) {
      let blackByte = 0;
      let redByte = 0;
      for (let bit = 0; bit < 8; bit++) {
        const index = ((y + bit) * SCREEN_SIZE + x) * 4;
        const r = pixels[index];
        const g = pixels[index + 1];
        const b = pixels[index + 2];
        const isRed = r > 150 && r > g * 1.6 && r > b * 1.6;
        const isBlack = !isRed && (r + g + b) < 300;
        if (!isBlack) blackByte |= 0x80 >> bit;
        if (isRed) redByte |= 0x80 >> bit;
      }
      black[output] = blackByte;
      red[output] = redByte;
      output++;
    }
  }
  return { black, red };
}

async function uploadPlane(plane, selector, start, end) {
  const chunkSize = 220;
  for (let offset = 0; offset < plane.length; offset += chunkSize) {
    const chunk = plane.subarray(offset, Math.min(offset + chunkSize, plane.length));
    const packet = new Uint8Array(chunk.length + 4);
    packet[0] = 0x03;
    packet[1] = selector;
    packet[2] = (offset >>> 8) & 0xff;
    packet[3] = offset & 0xff;
    packet.set(chunk, 4);
    await writeGatt(epdCharacteristic, packet);
    imageProgress.value = start + (offset + chunk.length) / plane.length * (end - start);
  }
}

async function uploadCanvas() {
  if (!epdCharacteristic) throw new Error("Connect to MAOWATCH first.");
  renderComposition();
  const { black, red } = canvasToPlanes(screenCanvas);
  uploadingImage = true;
  imageProgress.value = 0;
  setConnected(true);

  try {
    log("Preparing image mode and uploading the black plane…");
    await writeGatt(timeCharacteristic, new Uint8Array([0xe1, 0x00]));
    await writeGatt(epdCharacteristic, new Uint8Array([0x00, 0x00]));
    await uploadPlane(black, 0xff, 2, 48);
    log("Uploading the red plane…");
    await uploadPlane(red, 0x00, 48, 94);
    const fullRefresh = Number($("#refresh-mode").value);
    await writeGatt(epdCharacteristic, new Uint8Array([0x01, fullRefresh]));
    imageProgress.value = 100;

    log("Canvas sent. The watch is now showing the image.");
  } finally {
    uploadingImage = false;
    setConnected(Boolean(device?.gatt?.connected));
  }
}

async function showWatch() {
  if (!timeCharacteristic) throw new Error("Connect to MAOWATCH first.");
  await writeGatt(timeCharacteristic, new Uint8Array([0xe1, 0x01]));
  await writeGatt(timeCharacteristic, new Uint8Array([0xe2]));
  log("Clock scene restored. Choose Show image to resend the current canvas.");
}

function addressBytes(command, address) {
  return new Uint8Array([
    command,
    (address >>> 24) & 0xff,
    (address >>> 16) & 0xff,
    (address >>> 8) & 0xff,
    address & 0xff
  ]);
}

function firmwareChecksum(bytes) {
  let checksum = 0;
  for (let index = 0; index < OTA_MAX_SIZE; index++) {
    checksum = (checksum + (index < bytes.length ? bytes[index] : 0xff)) & 0xffff;
  }
  return checksum;
}

function waitForOtaNotification(timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      otaNotificationResolver = undefined;
      reject(new Error("Timed out waiting for the OTA checksum."));
    }, timeoutMs);
    otaNotificationResolver = value => {
      clearTimeout(timeout);
      resolve(value);
    };
  });
}

async function uploadFirmware() {
  if (!selectedFirmware || !otaCharacteristic) throw new Error("Select firmware and connect first.");
  const bytes = selectedFirmware;
  const checksum = firmwareChecksum(bytes);
  uploadingFirmware = true;
  otaProgress.value = 0;
  firmwareFile.disabled = true;
  setConnected(true);

  try {
    log(`Erasing OTA bank · checksum ${checksum.toString(16).padStart(4, "0").toUpperCase()}…`);
    for (let address = OTA_BANK_START; address < OTA_BANK_START + OTA_MAX_SIZE; address += 0x1000) {
      await writeGatt(otaCharacteristic, addressBytes(0x01, address));
      otaProgress.value = (address - OTA_BANK_START) / OTA_MAX_SIZE * 10;
    }

    for (let offset = 0; offset < bytes.length; offset += 0x100) {
      const page = bytes.subarray(offset, Math.min(offset + 0x100, bytes.length));
      for (let part = 0; part < page.length; part += 240) {
        const chunk = page.subarray(part, Math.min(part + 240, page.length));
        const packet = new Uint8Array(chunk.length + 1);
        packet[0] = 0x03;
        packet.set(chunk, 1);
        await writeGatt(otaCharacteristic, packet);
      }
      await writeGatt(otaCharacteristic, addressBytes(0x02, OTA_BANK_START + offset));
      otaProgress.value = 10 + (offset + page.length) / bytes.length * 80;
    }

    await writeGatt(otaCharacteristic, new Uint8Array([
      0x03, 0, 0, 0, 0, 0, (checksum >>> 8) & 0xff, checksum & 0xff
    ]));
    await writeGatt(otaCharacteristic, addressBytes(0x05, 0));
    const notification = waitForOtaNotification();
    await writeGatt(otaCharacteristic, new Uint8Array([0x06]));
    const result = await notification;
    const remoteChecksum = (result[1] << 8) | result[2];
    if (result[0] !== 0x07 || remoteChecksum !== checksum) {
      throw new Error(`OTA checksum mismatch: watch ${remoteChecksum.toString(16).padStart(4, "0")}, file ${checksum.toString(16).padStart(4, "0")}.`);
    }

    otaProgress.value = 95;
    log("Checksum verified. Installing firmware; the watch will reboot…");
    await writeGatt(otaCharacteristic, new Uint8Array([
      0x07, 0xc0, 0x01, 0xce, 0xed, (checksum >>> 8) & 0xff, checksum & 0xff
    ]));
    otaProgress.value = 100;
  } finally {
    uploadingFirmware = false;
    firmwareFile.disabled = false;
    setConnected(Boolean(device?.gatt?.connected));
  }
}

connectButton.addEventListener("click", async () => {
  try { await connect(); }
  catch (error) { setConnected(false); log(error.message || String(error), true); }
});
disconnectButton.addEventListener("click", disconnect);
syncButton.addEventListener("click", async () => {
  try { await syncTime(); }
  catch (error) { setConnected(Boolean(device?.gatt?.connected)); log(error.message || String(error), true); }
});
sendImageButton.addEventListener("click", async () => {
  try { await uploadCanvas(); }
  catch (error) { log(error.message || String(error), true); }
});
showWatchButton.addEventListener("click", async () => {
  try { await showWatch(); }
  catch (error) { log(error.message || String(error), true); }
});

imageFile.addEventListener("change", async () => {
  try { await loadImageFile(imageFile.files[0]); }
  catch (error) { log(error.message || String(error), true); }
});

for (const eventName of ["dragenter", "dragover"]) {
  dropzone.addEventListener(eventName, event => {
    event.preventDefault();
    dropzone.classList.add("dragging");
  });
}
for (const eventName of ["dragleave", "drop"]) {
  dropzone.addEventListener(eventName, event => {
    event.preventDefault();
    dropzone.classList.remove("dragging");
  });
}
dropzone.addEventListener("drop", async event => {
  try { await loadImageFile(event.dataTransfer.files[0]); }
  catch (error) { log(error.message || String(error), true); }
});

cropCanvas.addEventListener("pointerdown", startCropGesture);
cropCanvas.addEventListener("pointermove", updateCropGesture);
cropCanvas.addEventListener("pointerup", endCropGesture);
cropCanvas.addEventListener("pointercancel", endCropGesture);
resetCropButton.addEventListener("click", resetCrop);
rotateLeftButton.addEventListener("click", () => {
  imageRotation = (imageRotation + 270) % 360;
  queueRender();
});
rotateRightButton.addEventListener("click", () => {
  imageRotation = (imageRotation + 90) % 360;
  queueRender();
});
flipImageButton.addEventListener("click", () => {
  imageFlipped = !imageFlipped;
  flipImageButton.textContent = imageFlipped ? "↔ Flipped" : "↔ Flip";
  queueRender();
});
resetLookButton.addEventListener("click", () => {
  imageRotation = 0;
  imageFlipped = false;
  brightnessControl.value = 0;
  contrastControl.value = 0;
  redBiasControl.value = 0;
  brightnessValue.value = "0";
  contrastValue.value = "0";
  redValue.value = "0";
  flipImageButton.textContent = "↔ Flip";
  queueRender();
});

screenCanvas.addEventListener("pointerdown", screenPointerDown);
screenCanvas.addEventListener("pointermove", screenPointerMove);
screenCanvas.addEventListener("pointerup", screenPointerUp);
screenCanvas.addEventListener("pointercancel", screenPointerUp);

canvasTool.addEventListener("change", () => {
  const isDrawing = canvasTool.value === "draw" || canvasTool.value === "erase";
  screenCanvas.classList.toggle("drawing", isDrawing);
  if (isDrawing) screenCanvas.classList.remove("qr-active");
  else if (qrLayer || textLayer) screenCanvas.classList.add("qr-active");
});
brushSize.addEventListener("input", () => {
  brushSizeValue.value = `${brushSize.value} px`;
});
undoDrawingButton.addEventListener("click", () => {
  drawingStrokes.pop();
  queueRender();
});
clearDrawingButton.addEventListener("click", () => {
  drawingStrokes = [];
  queueRender();
});

for (const control of [paletteControl, ditherControl, brightnessControl, contrastControl, redBiasControl]) {
  control.addEventListener("input", () => {
    brightnessValue.value = brightnessControl.value;
    contrastValue.value = contrastControl.value;
    redValue.value = redBiasControl.value;
    queueRender();
  });
}

makeQrButton.addEventListener("click", () => {
  try { createQrLayer(); log("QR placed. Drag it directly on the watch canvas."); }
  catch (error) { log(error.message || String(error), true); }
});
qrSize.addEventListener("input", () => {
  qrSizeValue.value = `${qrSize.value} px`;
  if (qrLayer) {
    try { createQrLayer(); }
    catch (error) { log(error.message || String(error), true); }
  }
});
for (const control of [qrColor, qrLevel]) {
  control.addEventListener("change", () => {
    if (!qrLayer) return;
    try { createQrLayer(); }
    catch (error) { log(error.message || String(error), true); }
  });
}
removeQrButton.addEventListener("click", () => {
  qrLayer = undefined;
  removeQrButton.disabled = true;
  centerQrButton.disabled = true;
  if (!textLayer) screenCanvas.classList.remove("qr-active");
  queueRender();
});
centerQrButton.addEventListener("click", () => {
  if (!qrLayer) return;
  qrLayer.x = Math.round((SCREEN_SIZE - qrLayer.size) / 2);
  qrLayer.y = Math.round((SCREEN_SIZE - qrLayer.size) / 2);
  queueRender();
});

placeTextButton.addEventListener("click", () => {
  try { createTextLayer(); log("Text placed. Drag it directly on the watch canvas."); }
  catch (error) { log(error.message || String(error), true); }
});
textSize.addEventListener("input", () => {
  textSizeValue.value = `${textSize.value} px`;
  if (textLayer) {
    try { createTextLayer(); }
    catch (error) { log(error.message || String(error), true); }
  }
});
for (const control of [textColor, textFont]) {
  control.addEventListener("change", () => {
    if (!textLayer) return;
    try { createTextLayer(); }
    catch (error) { log(error.message || String(error), true); }
  });
}
removeTextButton.addEventListener("click", () => {
  textLayer = undefined;
  removeTextButton.disabled = true;
  if (!qrLayer) screenCanvas.classList.remove("qr-active");
  queueRender();
});

$("#clear-canvas").addEventListener("click", () => {
  sourceImage = undefined;
  sourceName = "";
  imageRotation = 0;
  imageFlipped = false;
  qrLayer = undefined;
  textLayer = undefined;
  drawingStrokes = [];
  imageFile.value = "";
  cropStage.classList.remove("visible");
  resetCropButton.disabled = true;
  rotateLeftButton.disabled = true;
  rotateRightButton.disabled = true;
  flipImageButton.disabled = true;
  resetLookButton.disabled = true;
  flipImageButton.textContent = "↔ Flip";
  removeQrButton.disabled = true;
  centerQrButton.disabled = true;
  removeTextButton.disabled = true;
  canvasTool.value = "select";
  screenCanvas.classList.remove("drawing");
  screenCanvas.classList.remove("qr-active");
  queueRender();
  log("Canvas cleared.");
});

$("#download-preview").addEventListener("click", () => {
  renderComposition();
  const link = document.createElement("a");
  const baseName = sourceName ? sourceName.replace(/\.[^.]+$/, "") : "maowatch";
  link.download = `${baseName}-200x200.png`;
  link.href = screenCanvas.toDataURL("image/png");
  link.click();
  log("Preview downloaded as a 200×200 PNG.");
});

firmwareFile.addEventListener("change", async () => {
  selectedFirmware = undefined;
  otaProgress.value = 0;
  const file = firmwareFile.files[0];
  if (!file) { setConnected(Boolean(device?.gatt?.connected)); return; }
  const bytes = new Uint8Array(await file.arrayBuffer());
  const signature = String.fromCharCode(...bytes.slice(8, 12));
  if (signature !== "KNLT") {
    firmwareFile.value = "";
    setConnected(Boolean(device?.gatt?.connected));
    log("Not a Telink firmware .bin: KNLT signature is missing.", true);
    return;
  }
  if (bytes.length > OTA_MAX_SIZE) {
    firmwareFile.value = "";
    setConnected(Boolean(device?.gatt?.connected));
    log(`Firmware is too large (${bytes.length} bytes; maximum ${OTA_MAX_SIZE}).`, true);
    return;
  }
  selectedFirmware = bytes;
  setConnected(Boolean(device?.gatt?.connected));
  log(`Selected ${file.name} · ${bytes.length} bytes.`);
});

uploadButton.addEventListener("click", async () => {
  try { await uploadFirmware(); }
  catch (error) { log(error.message || String(error), true); }
});

renderComposition();
updateClock();
setInterval(updateClock, 1000);
setConnected(false);
