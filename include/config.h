#ifndef CONFIG_H
#define CONFIG_H

// Frame buffer configuration
#define MAX_FRAME_WIDTH     640
#define MAX_FRAME_HEIGHT    480
#define MAX_FRAME_CHANNELS  3
#define MAX_FRAME_SIZE      (MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * MAX_FRAME_CHANNELS)

// MJPEG parser configuration
#define MJPEG_BUFFER_SIZE   (128 * 1024)  // 128KB for incoming data
#define MAX_JPEG_SIZE       (100 * 1024)  // 100KB max per JPEG frame

// URB configuration
#define NUM_URBS            4
#define MAX_ISO_PACKETS     32
#define MAX_PACKET_SIZE     3072
#define URB_BUFFER_SIZE     (MAX_ISO_PACKETS * MAX_PACKET_SIZE)

// Video configuration
#define DEFAULT_FPS         30
#define MAX_FRAMES          300

#endif // CONFIG_H
