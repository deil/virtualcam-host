const { ipcRenderer } = require('electron');

const startBtn = document.getElementById('startBtn');
const stopBtn = document.getElementById('stopBtn');
const sourceSelect = document.getElementById('sourceSelect');
const statusDiv = document.getElementById('status');
const previewVideo = document.getElementById('preview');

let isRunning = false;
let captureInterval = null;
let mediaStream = null;
let canvas, ctx;

const WIDTH = 1920;
const HEIGHT = 1080;

async function loadSources() {
  const sources = await ipcRenderer.invoke('camera:getSources');
  sourceSelect.innerHTML = sources.map(s =>
    `<option value="${s.id}">${s.name}</option>`
  ).join('');
}

async function startCapture() {
  const sourceId = sourceSelect.value;

  try {
    mediaStream = await navigator.mediaDevices.getUserMedia({
      audio: false,
      video: {
        mandatory: {
          chromeMediaSource: 'desktop',
          chromeMediaSourceId: sourceId,
          minWidth: WIDTH,
          maxWidth: WIDTH,
          minHeight: HEIGHT,
          maxHeight: HEIGHT
        }
      }
    });

    previewVideo.srcObject = mediaStream;
    previewVideo.play();

    canvas = document.createElement('canvas');
    canvas.width = WIDTH;
    canvas.height = HEIGHT;
    ctx = canvas.getContext('2d', { willReadFrequently: true });

    const result = await ipcRenderer.invoke('camera:start');
    if (!result.success) {
      throw new Error(result.error);
    }

    captureInterval = setInterval(() => {
      ctx.drawImage(previewVideo, 0, 0, WIDTH, HEIGHT);
      const imageData = ctx.getImageData(0, 0, WIDTH, HEIGHT);
      ipcRenderer.invoke('camera:writeFrame', imageData.data.buffer);
    }, 1000 / 30);

    isRunning = true;
    updateUI();

  } catch (error) {
    alert('Failed to start: ' + error.message);
    stopCapture();
  }
}

async function stopCapture() {
  if (captureInterval) {
    clearInterval(captureInterval);
    captureInterval = null;
  }

  if (mediaStream) {
    mediaStream.getTracks().forEach(t => t.stop());
    mediaStream = null;
  }

  previewVideo.srcObject = null;
  await ipcRenderer.invoke('camera:stop');

  isRunning = false;
  updateUI();
}

function updateUI() {
  if (isRunning) {
    startBtn.disabled = true;
    stopBtn.disabled = false;
    sourceSelect.disabled = true;
    statusDiv.className = 'status running';
    statusDiv.innerHTML = '<strong>Status:</strong> Streaming<div class="stats" id="stats"></div>';
  } else {
    startBtn.disabled = false;
    stopBtn.disabled = true;
    sourceSelect.disabled = false;
    statusDiv.className = 'status stopped';
    statusDiv.innerHTML = '<strong>Status:</strong> Stopped<div class="stats" id="stats"></div>';
  }
}

ipcRenderer.on('camera:stats', (event, data) => {
  const stats = document.getElementById('stats');
  if (stats) {
    stats.textContent = `Frames: ${data.frameCount}`;
  }
});

startBtn.addEventListener('click', startCapture);
stopBtn.addEventListener('click', stopCapture);

loadSources();
updateUI();
