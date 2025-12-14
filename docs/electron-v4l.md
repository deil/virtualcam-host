# Electron + V4L2 Virtual Camera

Summary of building a virtual webcam driver for Linux using Electron and v4l2loopback.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  RENDERER PROCESS (Chromium)                                │
│  - desktopCapturer gets screen/window                       │
│  - Canvas extracts RGBA pixels                              │
│  - Sends frames via IPC                                     │
└──────────────────────────┬──────────────────────────────────┘
                           │ ipcRenderer.invoke('camera:writeFrame', rgba)
┌──────────────────────────▼──────────────────────────────────┐
│  MAIN PROCESS (Node.js)                                     │
│  - Receives RGBA buffer                                     │
│  - Passes to native addon                                   │
└──────────────────────────┬──────────────────────────────────┘
                           │ camera.writeRgbaFrame(buffer)
┌──────────────────────────▼──────────────────────────────────┐
│  NATIVE ADDON (C++)                                         │
│  - RGBA → YUYV conversion                                   │
│  - write() to /dev/video10                                  │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│  v4l2loopback KERNEL MODULE                                 │
│  - Exposes /dev/video10                                     │
│  - Apps (Chrome, Zoom) read from it                         │
└─────────────────────────────────────────────────────────────┘
```

## v4l2loopback Setup

### Installation (Arch Linux)
```bash
sudo pacman -S v4l2loopback-dkms v4l2loopback-utils
```

### Loading the Module

**CRITICAL: Use `exclusive_caps=Y`**

```bash
sudo modprobe v4l2loopback devices=1 video_nr=10 card_label="Virtual Camera" exclusive_caps=Y
```

Without `exclusive_caps=Y`, the device advertises both OUTPUT and CAPTURE capabilities. Chrome filters out devices with OUTPUT capability, so the camera won't appear in the browser's camera list.

### Verify
```bash
# Check module loaded
lsmod | grep v4l2loopback

# Check exclusive_caps is Y
cat /sys/module/v4l2loopback/parameters/exclusive_caps

# Check device exists
v4l2-ctl --device=/dev/video10 --all
```

## Native Addon (node-addon-api)

### Why Native?
JavaScript is too slow for 1080p @ 30fps pixel conversion. Moving RGBA→YUYV to C++ gives 10-50x speedup.

### Structure
```
binding.gyp          # Build configuration
src/v4l2output.cc    # C++ implementation
lib/camera.js        # JavaScript wrapper
```

### binding.gyp
```json
{
  "targets": [{
    "target_name": "v4l2output",
    "sources": ["src/v4l2output.cc"],
    "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
    "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
    "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"]
  }]
}
```

### Key V4L2 Calls

```cpp
// 1. Open device
fd = open("/dev/video10", O_RDWR);

// 2. Set format
struct v4l2_format fmt;
fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
fmt.fmt.pix.width = 1920;
fmt.fmt.pix.height = 1080;
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
ioctl(fd, VIDIOC_S_FMT, &fmt);

// 3. Write frames (simple write, no mmap needed for v4l2loopback)
write(fd, yuyv_buffer, frame_size);

// 4. Close
close(fd);
```

### RGBA to YUYV Conversion

YUYV packs 2 pixels into 4 bytes: Y1 U Y2 V

```cpp
for (x = 0; x < width; x += 2) {
  // Get 2 adjacent pixels
  r1, g1, b1 = pixel[x]
  r2, g2, b2 = pixel[x+1]

  // Luminance per pixel
  y1 = 0.299*r1 + 0.587*g1 + 0.114*b1
  y2 = 0.299*r2 + 0.587*g2 + 0.114*b2

  // Chrominance averaged
  u = -0.169*avgR - 0.331*avgG + 0.5*avgB + 128
  v = 0.5*avgR - 0.419*avgG - 0.081*avgB + 128

  output = [y1, u, y2, v]
}
```

## Electron IPC

### Main Process (index.js)
```javascript
const { ipcMain } = require('electron');

ipcMain.handle('camera:writeFrame', async (event, rgbaData) => {
  const rgba = Buffer.from(rgbaData);
  camera.writeRgbaFrame(rgba);  // Calls C++
  return { success: true };
});
```

### Renderer Process (renderer.js)
```javascript
const { ipcRenderer } = require('electron');

// Capture screen
const stream = await navigator.mediaDevices.getUserMedia({
  video: { mandatory: { chromeMediaSource: 'desktop', chromeMediaSourceId: sourceId } }
});

// Extract pixels
ctx.drawImage(video, 0, 0, WIDTH, HEIGHT);
const imageData = ctx.getImageData(0, 0, WIDTH, HEIGHT);

// Send to main process
await ipcRenderer.invoke('camera:writeFrame', imageData.data.buffer);
```

## Troubleshooting

### Camera not showing in Chrome
1. Check `exclusive_caps=Y`: `cat /sys/module/v4l2loopback/parameters/exclusive_caps`
2. Restart Chrome completely (all processes)
3. Verify device has signal: `v4l2-ctl --device=/dev/video10 --all | grep "Video input"`
   - Should show "loopback: ok" not "loopback: no signal"

### "Device or resource busy"
```bash
lsof /dev/video10  # Find what's using it
kill <pid>         # Kill the process
```

### "Invalid argument" on start
- Usually means VIDIOC_STREAMON called when not needed
- v4l2loopback works with simple write() - no streaming setup required

### Testing without Chrome
```bash
# Capture with ffmpeg
ffmpeg -f v4l2 -i /dev/video10 -frames:v 1 test.jpg

# View with mpv
mpv /dev/video10
```

## Performance Notes

| Resolution | JS Conversion | C++ Conversion |
|------------|---------------|----------------|
| 640x480    | ~30 FPS       | ~60+ FPS       |
| 1920x1080  | ~5 FPS        | ~30 FPS        |

IPC overhead exists but is acceptable. For higher performance, consider SharedArrayBuffer or moving capture to main process.

## Files

```
camdriver-linux/
├── index.js           # Main process - IPC handlers, native addon calls
├── index.html         # UI
├── renderer.js        # Screen capture, sends frames via IPC
├── lib/camera.js      # JS wrapper for native addon
├── src/v4l2output.cc  # C++ native addon
├── binding.gyp        # Native addon build config
├── scripts/
│   ├── load-module.sh   # Load v4l2loopback with correct params
│   └── unload-module.sh # Unload module
└── docs/
    └── electron-v4l.md  # This file
```

## Dependencies

```json
{
  "devDependencies": {
    "electron": "^39.2.7",
    "node-addon-api": "^8.0.0",
    "node-gyp": "^10.0.0"
  }
}
```

## Lessons Learned

1. **exclusive_caps=Y is mandatory** for Chrome to see the camera
2. **v4l2loopback is simple** - just write() frames, no complex buffer management
3. **Move pixel conversion to C++** for acceptable performance at HD resolutions
4. **OBS source code is great reference** for V4L2 virtual camera implementation
5. **Device must be actively receiving frames** before apps will list it as available
