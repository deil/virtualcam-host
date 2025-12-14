# OBS Virtual Camera - Cross-Platform Implementation Analysis

Analysis of OBS Studio's virtual webcam feature across Linux, Windows, and macOS.

Source: `external/obs-studio/plugins/`

## Platform Comparison

| Platform | Location | Technology | Complexity |
|----------|----------|------------|------------|
| Linux | `linux-v4l2/` | v4l2loopback kernel module | ~360 lines |
| Windows | `win-dshow/virtualcam-module/` | DirectShow COM filter | ~600 lines |
| macOS | `mac-virtualcam/src/` | CoreMediaIO DAL + Camera Extension | ~2000+ lines |

---

## Linux Implementation

**Path:** `plugins/linux-v4l2/v4l2-output.c`

**How it works:**
1. Loads v4l2loopback kernel module with `exclusive_caps=1`
2. Opens `/dev/videoN` with `O_RDWR`
3. Sets format via `VIDIOC_S_FMT` ioctl
4. Writes YUYV frames via `write()` syscall

**Key code:**
```c
// Load module
system("modprobe v4l2loopback exclusive_caps=1 card_label='OBS Virtual Camera'");

// Open device
fd = open("/dev/video10", O_RDWR);

// Set format
struct v4l2_format fmt;
fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
ioctl(fd, VIDIOC_S_FMT, &fmt);

// Write frames
write(fd, frame_data, frame_size);
```

**Files:**
```
linux-v4l2/
├── v4l2-output.c      # Virtual camera output (what we care about)
├── v4l2-input.c       # Capturing from real webcams
├── v4l2-helpers.c/h   # Format negotiation
├── v4l2-controls.c/h  # Camera controls (brightness, etc.)
├── v4l2-udev.c/h      # Hot-plug detection
└── linux-v4l2.c       # Plugin entry point
```

**Why it's simple:** The kernel module (v4l2loopback) handles all the complexity of exposing a V4L2 device. OBS just writes frames.

### Kernel Module Loading & Privilege Elevation

OBS uses `pkexec` (PolicyKit) to load the kernel module - this shows a GUI password dialog rather than terminal prompt.

```c
// v4l2-output.c:64-104
static bool loopback_module_loaded()
{
    FILE *fp = fopen("/proc/modules", "r");
    // scans for "v4l2loopback" string
}

static int loopback_module_load()
{
    return run_command(
        "pkexec modprobe v4l2loopback exclusive_caps=1 card_label='OBS Virtual Camera' && sleep 0.5");
}
```

**Flow when user clicks "Start Virtual Camera":**
```
1. Check loopback_module_loaded() → reads /proc/modules
2. If not loaded → pkexec modprobe ...
3. PolicyKit shows GUI password dialog
4. User enters password once
5. Module loads, device available
```

**Privilege methods compared:**

| Method | Prompt Type | Use Case |
|--------|-------------|----------|
| `sudo` | Terminal | CLI apps |
| `pkexec` | GUI dialog | Desktop apps (OBS uses this) |

**Ways to avoid password prompts:**

1. **Load module at boot** (recommended):
   ```bash
   # /etc/modules-load.d/v4l2loopback.conf
   v4l2loopback

   # /etc/modprobe.d/v4l2loopback.conf
   options v4l2loopback devices=1 video_nr=10 card_label="Virtual Camera" exclusive_caps=1
   ```

2. **Passwordless PolicyKit rule:**
   ```javascript
   // /etc/polkit-1/rules.d/50-v4l2loopback.rules
   polkit.addRule(function(action, subject) {
       if (action.id == "org.freedesktop.policykit.exec" &&
           action.lookup("program") == "/sbin/modprobe" &&
           subject.isInGroup("video")) {
           return polkit.Result.YES;
       }
   });
   ```

3. **Passwordless sudo rule** (less secure):
   ```bash
   # /etc/sudoers.d/v4l2loopback
   %video ALL=(root) NOPASSWD: /sbin/modprobe v4l2loopback *
   ```

**Note:** OBS doesn't avoid the password - it just uses a GUI prompt instead of terminal.

---

## Windows Implementation

**Path:** `plugins/win-dshow/virtualcam-module/`

**How it works:**
1. Implements a DirectShow filter as a COM object
2. Registers DLL with `regsvr32` (requires admin)
3. Apps enumerate it via DirectShow/Media Foundation
4. OBS sends frames via shared memory

**Key files:**
```
virtualcam-module/
├── virtualcam-filter.cpp   # IBaseFilter implementation
├── virtualcam-filter.hpp
├── virtualcam-module.cpp   # DllMain, COM class factory
├── virtualcam-guid.h.in    # CLSID for the filter
├── placeholder.cpp         # Fallback "OBS not running" frame
├── sleepto.c/h             # Timing utilities
└── virtualcam-install.bat  # regsvr32 wrapper
```

**Key interfaces implemented:**
- `IBaseFilter` - DirectShow filter base
- `IPin` - Connection points
- `IAMStreamConfig` - Format negotiation
- `IKsPropertySet` - Device properties

**Why it's complex:** No kernel module equivalent. Must implement full DirectShow filter protocol, handle COM registration, and deal with Media Foundation compatibility.

---

## macOS Implementation

**Path:** `plugins/mac-virtualcam/src/`

**Two implementations exist:**

### 1. Legacy: CoreMediaIO DAL Plugin (pre-macOS 12.3)

**Path:** `src/dal-plugin/`

DAL = Device Abstraction Layer

```
dal-plugin/
├── OBSDALPlugIn.mm         # Plugin entry (CMIOHardwarePlugIn)
├── OBSDALDevice.mm         # Virtual device
├── OBSDALStream.mm         # Video stream
├── OBSDALMachClient.mm     # Mach IPC with OBS
└── CMSampleBufferUtils.mm  # Frame buffer handling
```

**How it works:**
1. Installs as `/Library/CoreMediaIO/Plug-Ins/DAL/obs-mac-virtualcam.plugin`
2. Implements `CMIOHardwarePlugIn` interface
3. OBS sends frames via Mach ports (XPC)
4. Apps see it as a camera via AVFoundation

### 2. Modern: Camera Extension (macOS 12.3+)

**Path:** `src/camera-extension/`

Apple's new system extension model (more secure, sandboxed).

```
camera-extension/
├── main.swift                    # Extension entry
├── OBSCameraProviderSource.swift # Provides the device
├── OBSCameraDeviceSource.swift   # The virtual device
├── OBSCameraStreamSource.swift   # Video stream
└── OBSCameraStreamSink.swift     # Receives frames from OBS
```

**Why two implementations:** Apple deprecated DAL plugins. Camera Extension is the future but requires macOS 12.3+. OBS ships both for compatibility.

**Why it's the most complex:**
- Must implement Apple's proprietary interfaces
- Two completely separate implementations
- Mach/XPC IPC for frame transfer
- Code signing and notarization requirements
- System extension approval flow

---

## Frame Transfer Mechanisms

| Platform | Mechanism |
|----------|-----------|
| Linux | Direct `write()` to device file |
| Windows | Shared memory (named pipe/file mapping) |
| macOS | Mach ports / XPC |

---

## Installation Requirements

| Platform | Requirement |
|----------|-------------|
| Linux | `modprobe v4l2loopback` (root) |
| Windows | `regsvr32` registration (admin) |
| macOS | System extension approval (user) |

---

## Key Learnings

1. **Linux is simplest** because the kernel module does the heavy lifting. User-space just writes frames.

2. **Windows requires COM** - must implement DirectShow interfaces. No OS-provided "virtual device" helper.

3. **macOS is most complex** - two implementations, proprietary APIs, strict code signing.

4. **All platforms need elevated privileges** to install the virtual device.

5. **Frame format is typically YUYV** across all platforms (packed 4:2:2).

6. **IPC varies significantly** - Linux uses device files, Windows uses shared memory, macOS uses Mach IPC.

---

## Source Reference

OBS Studio repository structure:
```
obs-studio/plugins/
├── linux-v4l2/                    # Linux V4L2 input + virtual output
├── win-dshow/
│   ├── virtualcam-module/         # Windows virtual camera
│   └── ... (other DirectShow code)
└── mac-virtualcam/
    └── src/
        ├── dal-plugin/            # Legacy macOS (< 12.3)
        ├── camera-extension/      # Modern macOS (>= 12.3)
        ├── common/                # Shared IPC definitions
        └── obs-plugin/            # OBS-side frame sender
```
