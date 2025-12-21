# 📹 USB Camera Driver from Scratch

A bare-metal USB Video Class (UVC) camera driver implementation in C without external libraries (except libjpeg). Features real-time video capture, MJPEG decoding, image processing, and MP4 output - all optimized for embedded Linux systems.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-blue.svg)](https://www.kernel.org/)
[![Language: C](https://img.shields.io/badge/Language-C-green.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

## 🌟 Features

- ✅ **No libusb/v4l2** - Direct kernel USB interface via ioctl
- ✅ **Embedded-friendly** - Fixed memory buffers, no malloc/free
- ✅ **UVC Protocol** - Full USB Video Class implementation
- ✅ **MJPEG Parsing** - Frame extraction from video stream
- ✅ **Image Processing** - OpenCV-like operations (brightness, contrast, drawing)
- ✅ **MP4 Output** - H.264 encoded video via ffmpeg
- ✅ **Real-time** - Isochronous USB transfers for continuous streaming


## 📋 Table of Contents

- [Requirements](#requirements)
- [System Compatibility](#system-compatibility)
- [Installation](#installation)
- [Project Structure](#project-structure)
- [Building](#building)
- [Usage](#usage)
- [Configuration](#configuration)
- [Image Processing](#image-processing)
- [How It Works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)
- [Contributing](#contributing)
- [License](#license)

## 🔧 Requirements

### Operating System
- **Linux Kernel**: 3.0+ (tested on 5.x, 6.x)
- **Architecture**: x86, x86_64, ARM, ARM64
- **Tested Distributions**:
  - Ubuntu 20.04, 22.04, 24.04

### Software Dependencies
```bash
# Required
gcc >= 7.0
make
libjpeg-dev (libjpeg-turbo or libjpeg9)
ffmpeg (for MP4 conversion)

# Optional
lsusb (usbutils) - for device discovery
```

### Hardware Requirements
- **RAM**: 2 MB minimum (embedded), 64+ MB recommended
- **USB Camera**: Any UVC-compliant camera (most webcams)
- **USB Port**: USB 2.0 or higher

### Permissions
- Root access or udev rules for USB device access
- Ability to detach kernel drivers (uvcvideo module)

## 🖥️ System Compatibility

### Supported Platforms

| Platform | Architecture | Status | Notes |
|----------|-------------|--------|-------|
| Raspberry Pi 4B | ARM64 | ✅ Tested | Main development platform |
| Ubuntu 24.04 | x86_64 | ✅ Tested | Full features |

### Tested Cameras

| Camera | Vendor:Product | Format | Status |
|--------|---------------|---------|---------|
| Logitech C270 | 046d:0825 | MJPEG | ✅ Verified |


## 📦 Installation

### Quick Start

```bash
# 1. Clone repository
git clone https://github.com/climber0824/USB_CAM
cd USB_CAM

# 2. Install dependencies
# Ubuntu
sudo apt-get update
sudo apt-get install build-essential libjpeg-dev ffmpeg

# 3. Build
make

# 4. Find your camera
lsusb | grep -i camera

# 5. Run (replace device path)
sudo ./uvc_camera /dev/bus/usb/001/003
```

### Detailed Installation

#### For Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    gcc \
    make \
    libjpeg-dev \
    libjpeg-turbo8-dev \
    ffmpeg \
    usbutils

git clone https://github.com/climber0824/USB_CAM
cd USB_CAM
make
```

## 📁 Project Structure

```
uvc-camera-driver/
├── README.md                   # This file
├── LICENSE                     # MIT License
├── Makefile                    # Build configuration
│
├── include/                  # Header files
│   ├── config.h               # System configuration (memory, buffers)
│   ├── uvc_camera.h           # UVC protocol definitions
│   ├── image_processing.h     # Image processing functions
│   ├── mjpeg_parser.h         # MJPEG stream parser
│   └── urb_manager.h          # USB Request Block management
│
├── src/                      # Implementation files
│   ├── uvc_camera.c           # UVC protocol implementation
│   ├── image_processing.c     # Image processing operations
│   ├── mjpeg_parser.c         # MJPEG frame extraction
│   └── urb_manager.c          # URB submission/reaping
│
└── execute/                  # Application entry point
   └── main.c                  # Main program with capture loop

```

## 🔨 Building

### Basic Build
```bash
make                    # Build project
make clean              # Clean build artifacts
make all                # Clean + build
```

### Build Options
```bash
# Debug build with symbols
make CFLAGS="-g -DDEBUG"

# Optimized build
make CFLAGS="-O3 -march=native"

# Static build (for embedded deployment)
make LDFLAGS="-static"

# Verbose build
make V=1
```

### Cross-Compilation
```bash
# ARM (Raspberry Pi)
make CC=arm-linux-gnueabihf-gcc

# ARM64
make CC=aarch64-linux-gnu-gcc

# With custom toolchain
make CC=/opt/toolchain/bin/arm-gcc \
     CFLAGS="-I/opt/sysroot/include" \
     LDFLAGS="-L/opt/sysroot/lib"
```

## 🚀 Usage

### Basic Usage

```bash
# 1. Find your camera device
lsusb
# Output: Bus 001 Device 003: ID 046d:0825 Logitech Webcam C270

# 2. Run the program
sudo ./uvc_camera /dev/bus/usb/001/003

# 3. Output files created:
# - output.rgb (raw RGB frames)
# - output.mp4 (H.264 encoded video)
```

### Advanced Usage

```bash
# Specify custom output file
sudo ./uvc_camera /dev/bus/usb/001/003 my_capture.rgb

# Capture with specific parameters (edit config.h first)
# Then rebuild and run
sudo ./uvc_camera /dev/bus/usb/001/003

# Debug mode (if compiled with -DDEBUG)
sudo ./uvc_camera /dev/bus/usb/001/003 2>&1 | tee debug.log
```

### Setting Up udev Rules (No sudo required)

```bash
# Create udev rule
sudo nano /etc/udev/rules.d/99-uvc-camera.rules

# Add this line (replace vendor:product with yours)
SUBSYSTEM=="usb", ATTRS{idVendor}=="046d", ATTRS{idProduct}=="0825", MODE="0666"

# Reload rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# Now run without sudo
./uvc_camera /dev/bus/usb/001/003
```

## ⚙️ Configuration

### Memory Configuration

Edit `include/config.h`:

```c
// For high-end systems (1080p)
#define MAX_FRAME_WIDTH     1920
#define MAX_FRAME_HEIGHT    1080
#define MJPEG_BUFFER_SIZE   (256 * 1024)
#define NUM_URBS            8

// For embedded systems (VGA)
#define MAX_FRAME_WIDTH     640
#define MAX_FRAME_HEIGHT    480
#define MJPEG_BUFFER_SIZE   (128 * 1024)
#define NUM_URBS            4

// For low-memory systems (QVGA)
#define MAX_FRAME_WIDTH     320
#define MAX_FRAME_HEIGHT    240
#define MJPEG_BUFFER_SIZE   (64 * 1024)
#define NUM_URBS            2
```

### Camera Configuration

Edit in `execute/main.c`:

```c
// Video format
ctrl.bFormatIndex = 1;      // 1=MJPEG, 2=YUV (check with lsusb -v)
ctrl.bFrameIndex = 1;       // 1=highest res, higher=lower res
ctrl.dwFrameInterval = 333333;  // Frame rate (333333 = 30fps)

// Frame capture limit
#define MAX_FRAMES 300      // Number of frames to capture

// USB endpoint (usually 0x81, check with lsusb -v)
int endpoint = 0x81;
```

## 🎨 Image Processing

### Available Operations

All operations work on fixed-size `Image` structures (no malloc):

```c
// In execute/main.c, process_frame() function:

void process_frame(Image *img, int frame_number, FILE *output) {
    // 1. Draw rectangle (x, y, width, height, R, G, B, thickness)
    image_draw_rect(img, 100, 100, 200, 150, 0, 255, 0, 3);
    
    // 2. Draw line (x1, y1, x2, y2, R, G, B)
    image_draw_line(img, 0, 0, img->width, img->height, 255, 0, 0);
    
    // 3. Adjust brightness (-255 to +255)
    image_adjust_brightness(img, 30);
    
    // 4. Adjust contrast (0.5 = less, 2.0 = more)
    image_adjust_contrast(img, 1.5);
    
    // 5. Convert to grayscale
    image_to_grayscale(img);
    
    // 6. Set individual pixel (x, y, R, G, B)
    image_set_pixel(img, 10, 10, 255, 0, 0);
    
    // 7. Get pixel color
    uint8_t r, g, b;
    image_get_pixel(img, 10, 10, &r, &g, &b);
}
```

### Example: Object Detection Overlay

```c
void process_frame(Image *img, int frame_number, FILE *output) {
    // Simulate object detection at center
    int obj_x = img->width / 2 - 50;
    int obj_y = img->height / 2 - 50;
    int obj_w = 100;
    int obj_h = 100;
    
    // Draw bounding box
    image_draw_rect(img, obj_x, obj_y, obj_w, obj_h, 0, 255, 0, 3);
    
    // Draw crosshair at center
    int cx = img->width / 2;
    int cy = img->height / 2;
    image_draw_line(img, cx - 20, cy, cx + 20, cy, 255, 0, 0);
    image_draw_line(img, cx, cy - 20, cx, cy + 20, 255, 0, 0);
    
    // Enhance image
    image_adjust_brightness(img, 10);
    image_adjust_contrast(img, 1.2);
    
    // Write to output
    fwrite(img->data, 1, img->height * img->step, output);
}
```

## 🔍 How It Works

### System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      USB Camera Hardware                    │
└────────────────────┬────────────────────────────────────────┘
                     │ USB 2.0/3.0 (Isochronous Transfers)
┌────────────────────▼────────────────────────────────────────┐
│                    Linux USB Subsystem                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ usbdevfs API │  │  Kernel URBs │  │  Endpoints   │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└────────────────────┬────────────────────────────────────────┘
                     │ ioctl() system calls
┌────────────────────▼────────────────────────────────────────┐
│                     Our Application                         │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ UVC Protocol │→ │ MJPEG Parser │→ │Image Processor│      │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│         ↓                  ↓                 ↓              │
│  ┌──────────────────────────────────────────────────┐       │
│  │           URB Manager (4 URBs in flight)         │       │
│  └──────────────────────────────────────────────────┘       │
└────────────────────┬────────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────────┐
│                       Output Files                          │
│    output.rgb (raw) → ffmpeg → output.mp4 (H.264)           │
└─────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **USB Setup**
   - Open device (`/dev/bus/usb/BBB/DDD`)
   - Detach kernel driver (uvcvideo)
   - Claim interfaces (0=control, 1=streaming)

2. **Negotiation** (Probe & Commit)
   - Send desired parameters (format, resolution, fps)
   - Camera responds with supported values
   - Finalize settings

3. **Streaming**
   - Set alternate interface (enables endpoint)
   - Submit URBs for isochronous transfers
   - Reap completed URBs
   - Extract UVC payload headers
   - Parse MJPEG frames

4. **Processing**
   - Decode JPEG to RGB
   - Apply image processing
   - Write to output file

5. **Conversion**
   - Use ffmpeg to convert RGB → MP4

### Key Components

**URB (USB Request Block)**
- Kernel structure for async USB I/O
- Contains buffer, packet descriptors, completion callback
- We use isochronous type for guaranteed bandwidth

**UVC Protocol**
- Standard for USB cameras
- Defines video controls, formats, streaming
- Probe/Commit negotiation for parameters

**MJPEG Parsing**
- Search for JPEG markers (0xFFD8 start, 0xFFD9 end)
- Extract complete frames from stream
- Handle partial data across packets

## 🐛 Troubleshooting

### Common Issues

#### "Device or resource busy"
```bash
# Problem: Kernel driver already attached
# Solution: Driver detachment is automatic, but if it fails:

# Check what's using camera
lsof /dev/video*
sudo pkill cheese  # or other program

# Manually unload driver
sudo rmmod uvcvideo

# Run program
sudo ./uvc_camera /dev/bus/usb/001/003

# Reload driver when done
sudo modprobe uvcvideo
```

#### "Message too long" when submitting URB
```bash
# Problem: Packet size too large for alternate setting
# Solution: Program auto-detects, but if it fails:

# Check camera's alternate settings
sudo lsusb -v -d VENDOR:PRODUCT | grep -A 10 "bAlternateSetting"

# Look for wMaxPacketSize values
# Lower alt settings have smaller packet sizes

# Edit main.c to try specific alt setting:
set_interface_alt_setting(fd_usb, 1, 7);  // Try 7, 6, 5...
```

#### "Failed to open USB device"
```bash
# Problem: Permission denied
# Solution 1: Run with sudo
sudo ./uvc_camera /dev/bus/usb/001/003

# Solution 2: Add udev rule (permanent)
# See "Setting Up udev Rules" section above
```

#### No video output / blank frames
```bash
# Check if camera supports MJPEG
sudo lsusb -v -d VENDOR:PRODUCT | grep -i mjpeg

# If camera uses YUV instead:
# Edit main.c: ctrl.bFormatIndex = 2; (2 usually = YUV)
# Need to add YUV→RGB conversion code

# Check frame format with:
file output.rgb
# Should show: data
```

#### Low frame rate / choppy video
```bash
# Increase URB count in config.h:
#define NUM_URBS 8  // More URBs = smoother streaming

# Reduce resolution in config.h:
#define MAX_FRAME_WIDTH 320
#define MAX_FRAME_HEIGHT 240

# Check USB bus speed:
lsusb -t
# Should show 480M (USB 2.0) or 5000M (USB 3.0)
```

### Debug Mode

```bash
# Compile with debug symbols
make clean
make CFLAGS="-g -DDEBUG"

# Run with gdb
sudo gdb ./uvc_camera
(gdb) run /dev/bus/usb/001/003

# Run with strace to see system calls
sudo strace -o trace.log ./uvc_camera /dev/bus/usb/001/003
grep ioctl trace.log
```

## 📚 Technical Details

### Memory Usage

| Component | Size | Purpose |
|-----------|------|---------|
| URB Buffers | ~400 KB | 4 URBs × 100KB each |
| MJPEG Parser | ~128 KB | Incoming data buffer |
| Current Frame | ~900 KB | 640×480×3 RGB image |
| JPEG Buffer | ~100 KB | Decoded frame storage |
| **Total** | **~1.5 MB** | **Static allocation** |

### Performance

| Metric | Value | Notes |
|--------|-------|-------|
| Frame Rate | 30 fps | Configurable |
| Latency | ~50ms | USB + processing |
| CPU Usage | ~15% | Raspberry Pi 4B @ 1.5GHz |
| Memory | Fixed | No runtime allocation |
| USB Bandwidth | ~25 Mbps | 640×480 MJPEG |

### USB Details

**Transfer Type**: Isochronous
- Guaranteed bandwidth
- No retransmission
- Acceptable data loss
- Used for video/audio streaming

**Endpoints**:
- 0x00: Control (bidirectional)
- 0x81: Video IN (camera → host)

**Packet Structure**:
```
┌─────────────┬──────────────────────┐
│ UVC Header  │  MJPEG Payload Data  │
│  (2-12 B)   │  (variable length)   │
└─────────────┴──────────────────────┘
```

### Supported Formats

| Format | bFormatIndex | Status | Notes |
|--------|--------------|--------|-------|
| MJPEG | 1 | ✅ Full support | Most common |
| Uncompressed YUV | 2 | ⚠️ Needs conversion | Not implemented |
| H.264 | 16-19 | ❌ Not supported | Requires different parser |

## 🤝 Contributing

Contributions welcome! Please follow these guidelines:

### Development Setup
```bash
git clone https://github.com/climber0824/USB_CAM
cd USB_CAM
git checkout -b feature/your-feature

# Make changes
make clean && make

# Test
sudo ./uvc_camera /dev/bus/usb/001/003

# Commit
git add .
git commit -m "Add: your feature description"
git push origin feature/your-feature
```

### Code Style
- Follow Linux kernel coding style
- Use 4 spaces for indentation
- Max 100 characters per line
- Comment complex logic

### Testing Checklist
- [ ] Compiles without warnings
- [ ] Tested on target hardware
- [ ] Memory usage within limits
- [ ] No memory leaks (valgrind)
- [ ] Documentation updated

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Linux USB Subsystem documentation
- USB Video Class specification (USB.org)
- libjpeg-turbo project
- FFmpeg project

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/climber0824/USB_CAM/issues)
- **Email**: meteorx900824@gmail.com

## 🗺️ Roadmap

- [ ] YUV format support
- [ ] H.264 hardware decoding
- [ ] Multiple camera support
- [ ] Real-time streaming (RTSP/WebRTC)
- [ ] Advanced image processing (filters, effects)

---
