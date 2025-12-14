#include <napi.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <errno.h>

class V4L2Output : public Napi::ObjectWrap<V4L2Output> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  V4L2Output(const Napi::CallbackInfo& info);
  ~V4L2Output();

private:
  int fd;
  uint32_t width;
  uint32_t height;
  uint32_t pixelformat;
  uint8_t* yuyv_buffer;

  Napi::Value Open(const Napi::CallbackInfo& info);
  Napi::Value SetFormat(const Napi::CallbackInfo& info);
  Napi::Value WriteFrame(const Napi::CallbackInfo& info);
  Napi::Value WriteRgbaFrame(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);
};

V4L2Output::V4L2Output(const Napi::CallbackInfo& info) : Napi::ObjectWrap<V4L2Output>(info) {
  fd = -1;
  width = 0;
  height = 0;
  pixelformat = V4L2_PIX_FMT_YUYV;
  yuyv_buffer = nullptr;
}

V4L2Output::~V4L2Output() {
  if (fd >= 0) {
    close(fd);
  }
  if (yuyv_buffer) {
    delete[] yuyv_buffer;
  }
}

Napi::Object V4L2Output::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "V4L2Output", {
    InstanceMethod("open", &V4L2Output::Open),
    InstanceMethod("setFormat", &V4L2Output::SetFormat),
    InstanceMethod("writeFrame", &V4L2Output::WriteFrame),
    InstanceMethod("writeRgbaFrame", &V4L2Output::WriteRgbaFrame),
    InstanceMethod("close", &V4L2Output::Close),
  });

  Napi::FunctionReference* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("V4L2Output", func);
  return exports;
}

Napi::Value V4L2Output::Open(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "String expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string device = info[0].As<Napi::String>().Utf8Value();

  fd = open(device.c_str(), O_RDWR);
  if (fd < 0) {
    Napi::Error::New(env, std::string("Failed to open device: ") + strerror(errno))
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Boolean::New(env, true);
}

Napi::Value V4L2Output::SetFormat(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (fd < 0) {
    Napi::Error::New(env, "Device not opened").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
    Napi::TypeError::New(env, "Width and height expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  width = info[0].As<Napi::Number>().Uint32Value();
  height = info[1].As<Napi::Number>().Uint32Value();

  struct v4l2_format fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  fmt.fmt.pix.width = width;
  fmt.fmt.pix.height = height;
  fmt.fmt.pix.pixelformat = pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_NONE;
  fmt.fmt.pix.bytesperline = width * 2; // YUYV is 2 bytes per pixel
  fmt.fmt.pix.sizeimage = width * height * 2;
  fmt.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;

  if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
    Napi::Error::New(env, std::string("Failed to set format: ") + strerror(errno))
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  // Set stream parameters (FPS) - optional, ignore errors
  struct v4l2_streamparm parm;
  memset(&parm, 0, sizeof(parm));
  parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  parm.parm.output.capability = V4L2_CAP_TIMEPERFRAME;
  parm.parm.output.timeperframe.numerator = 1;
  parm.parm.output.timeperframe.denominator = 30;
  ioctl(fd, VIDIOC_S_PARM, &parm); // Best effort

  // Allocate conversion buffer
  if (yuyv_buffer) delete[] yuyv_buffer;
  yuyv_buffer = new uint8_t[width * height * 2];

  return Napi::Boolean::New(env, true);
}

Napi::Value V4L2Output::WriteFrame(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (fd < 0) {
    Napi::Error::New(env, "Device not opened").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "Buffer expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Buffer<uint8_t> buffer = info[0].As<Napi::Buffer<uint8_t>>();
  size_t expected_size = width * height * 2; // YUYV

  if (buffer.Length() < expected_size) {
    Napi::Error::New(env, "Buffer too small").ThrowAsJavaScriptException();
    return env.Null();
  }

  ssize_t written = write(fd, buffer.Data(), expected_size);
  if (written < 0) {
    Napi::Error::New(env, std::string("Failed to write frame: ") + strerror(errno))
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, written);
}

Napi::Value V4L2Output::WriteRgbaFrame(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (fd < 0 || !yuyv_buffer) {
    Napi::Error::New(env, "Device not ready").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "Buffer expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Buffer<uint8_t> rgba = info[0].As<Napi::Buffer<uint8_t>>();
  size_t expected_rgba = width * height * 4;

  if (rgba.Length() < expected_rgba) {
    Napi::Error::New(env, "RGBA buffer too small").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint8_t* src = rgba.Data();
  uint8_t* dst = yuyv_buffer;

  // Fast RGBA to YUYV conversion
  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x += 2) {
      size_t i1 = (y * width + x) * 4;
      size_t i2 = i1 + 4;

      uint8_t r1 = src[i1], g1 = src[i1 + 1], b1 = src[i1 + 2];
      uint8_t r2 = src[i2], g2 = src[i2 + 1], b2 = src[i2 + 2];

      int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
      int y2 = ((66 * r2 + 129 * g2 + 25 * b2 + 128) >> 8) + 16;

      int avgR = (r1 + r2) >> 1;
      int avgG = (g1 + g2) >> 1;
      int avgB = (b1 + b2) >> 1;

      int u = ((-38 * avgR - 74 * avgG + 112 * avgB + 128) >> 8) + 128;
      int v = ((112 * avgR - 94 * avgG - 18 * avgB + 128) >> 8) + 128;

      size_t out = (y * width + x) * 2;
      dst[out] = y1 > 255 ? 255 : (y1 < 0 ? 0 : y1);
      dst[out + 1] = u > 255 ? 255 : (u < 0 ? 0 : u);
      dst[out + 2] = y2 > 255 ? 255 : (y2 < 0 ? 0 : y2);
      dst[out + 3] = v > 255 ? 255 : (v < 0 ? 0 : v);
    }
  }

  ssize_t written = write(fd, yuyv_buffer, width * height * 2);
  if (written < 0) {
    Napi::Error::New(env, std::string("Failed to write frame: ") + strerror(errno))
      .ThrowAsJavaScriptException();
    return env.Null();
  }

  return Napi::Number::New(env, written);
}

Napi::Value V4L2Output::Close(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (fd >= 0) {
    close(fd);
    fd = -1;
  }

  return Napi::Boolean::New(env, true);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  return V4L2Output::Init(env, exports);
}

NODE_API_MODULE(v4l2output, Init)
