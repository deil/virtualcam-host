const { VirtualCamera } = require('./lib/camera');

const WIDTH = 640;
const HEIGHT = 480;
const FRAME_SIZE = WIDTH * HEIGHT * 2; // YUYV = 2 bytes per pixel

console.log('Opening /dev/video10...');
const cam = new VirtualCamera('/dev/video10');
cam.open();

console.log(`Setting format to ${WIDTH}x${HEIGHT}...`);
cam.setFormat(WIDTH, HEIGHT);

console.log('Writing frames (Ctrl+C to stop)...');

let frameCount = 0;
let hue = 0;

function createColorFrame(h) {
  const buffer = Buffer.alloc(FRAME_SIZE);

  // Simple color pattern - YUYV format
  // Y = luminance, U/V = chrominance
  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      const offset = (y * WIDTH + x) * 2;

      // Animated gradient based on hue
      const Y = ((x + h) % 255);
      const U = ((y + h) % 255) - 128;
      const V = ((x + y + h) % 255) - 128;

      buffer[offset] = Y;
      buffer[offset + 1] = (x % 2 === 0) ? U : V;
    }
  }

  return buffer;
}

const interval = setInterval(() => {
  const frame = createColorFrame(hue);
  cam.writeFrame(frame);
  frameCount++;
  hue = (hue + 1) % 256;

  if (frameCount % 30 === 0) {
    console.log(`Frames written: ${frameCount}`);
  }
}, 1000 / 30); // 30 FPS

process.on('SIGINT', () => {
  console.log('\nStopping...');
  clearInterval(interval);
  cam.close();
  console.log('Closed.');
  process.exit(0);
});
