const { V4L2Output } = require('../build/Release/v4l2output.node');

class VirtualCamera {
  constructor(device = '/dev/video10') {
    this.device = device;
    this.output = new V4L2Output();
    this.width = 0;
    this.height = 0;
  }

  open() {
    this.output.open(this.device);
  }

  setFormat(width, height) {
    this.width = width;
    this.height = height;
    this.output.setFormat(width, height);
  }

  writeFrame(buffer) {
    return this.output.writeFrame(buffer);
  }

  writeRgbaFrame(buffer) {
    return this.output.writeRgbaFrame(buffer);
  }

  close() {
    this.output.close();
  }
}

module.exports = { VirtualCamera };
