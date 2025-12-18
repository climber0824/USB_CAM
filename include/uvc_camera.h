#ifndef UVC_CAMERA_H
#define UVC_CAMERA_H

#include <stdint.h>
#include <linux/usbdevice_fs.h>

// UVC specific definitions
#define UVC_SET_CUR     0x01
#define UVC_GET_CUR     0x81
#define UVC_GET_MIN     0x82
#define UVC_GET_MAX     0x83
#define UVC_GET_RES     0x84

#define UVC_VS_PROBE_CONTROL    0x01
#define UVC_VS_COMMIT_CONTROL   0x02

// Video streaming interface control
#define USB_VIDEO_CONTROL_INTERFACE    0
#define USB_VIDEO_STREAMING_INTERFACE  1

// UVC Video Probe and Commit Controls structure
struct uvc_streaming_control {
    uint16_t bmHint;
    uint8_t  bFormatIndex;
    uint8_t  bFrameIndex;
    uint32_t dwFrameInterval;
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
    uint32_t dwClockFrequency;
    uint8_t  bmFramingInfo;
    uint8_t  bPreferedVersion;
    uint8_t  bMinVersion;
    uint8_t  bMaxVersion;
} __attribute__((packed));

// URB (USB Request Block) for isochronous transfers
struct uvc_urb {
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc iso_packets[32];
    unsigned char buffer[32 * 3072];
};

// Function declarations
void print_streaming_control(struct uvc_streaming_control *ctrl);

int uvc_control_query(int fd, uint8_t request, uint8_t unit_id, 
                      uint8_t interface, uint8_t cs, void *data, uint16_t size);

int uvc_probe_commit(int fd, struct uvc_streaming_control *ctrl, int probe);

int set_interface_alt_setting(int fd, int interface, int alt_setting);

int claim_interface(int fd, int interface);

int release_interface(int fd, int interface);

int submit_iso_urb(int fd, struct uvc_urb *urb_data, int endpoint, 
                   int num_packets, int packet_size);

struct usbdevfs_urb *reap_urb(int fd, int timeout_ms);

void process_video_data(unsigned char *data, int length, FILE *output);

#endif // UVC_CAMERA_H
