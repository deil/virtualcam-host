const { app, BrowserWindow, ipcMain, desktopCapturer } = require('electron');
const { VirtualCamera } = require('./lib/camera');

let mainWindow;
let camera;
let frameCount = 0;

const WIDTH = 1920;
const HEIGHT = 1080;

ipcMain.handle('camera:getSources', async () => {
  const sources = await desktopCapturer.getSources({ types: ['screen', 'window'] });
  return sources.map(s => ({ id: s.id, name: s.name }));
});

ipcMain.handle('camera:start', async () => {
  try {
    if (camera) {
      return { success: false, error: 'Camera already running' };
    }

    camera = new VirtualCamera('/dev/video10');
    camera.open();
    camera.setFormat(WIDTH, HEIGHT);
    frameCount = 0;

    return { success: true };
  } catch (error) {
    return { success: false, error: error.message };
  }
});

ipcMain.handle('camera:writeFrame', async (event, rgbaData) => {
  if (!camera) return { success: false };

  try {
    const rgba = Buffer.from(rgbaData);
    camera.writeRgbaFrame(rgba); // C++ does conversion now
    frameCount++;

    if (frameCount % 30 === 0) {
      mainWindow?.webContents.send('camera:stats', { frameCount });
    }

    return { success: true };
  } catch (error) {
    return { success: false, error: error.message };
  }
});

ipcMain.handle('camera:stop', async () => {
  try {
    if (camera) {
      camera.close();
      camera = null;
    }

    return { success: true };
  } catch (error) {
    return { success: false, error: error.message };
  }
});

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false
    }
  });

  mainWindow.loadFile('index.html');
  mainWindow.webContents.openDevTools();

  mainWindow.on('closed', () => {
    if (cameraInterval) {
      clearInterval(cameraInterval);
    }
    if (camera) {
      camera.close();
    }
  });
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', function () {
  if (process.platform !== 'darwin') app.quit();
});
