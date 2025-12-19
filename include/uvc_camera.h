#ifndef UVC_CAMERA_H
#define UVC_CAMERA_H

#include <stdint.h>
#include <stdio.h>
#include <linux/usb/video.h>
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

// URB for isochronous transfers
#define MAX_ISO_PACKETS 32
#define MAX_PACKET_SIZE 3072


// URB (USB Request Block) for isochronous transfers
struct uvc_urb {
    struct usbdevfs_urb urb;
};

// Seperate buffer for actual data
struct uvc_transfer {
    struct uvc_urb urb_header;
    struct usbdevfs_iso_packet_desc iso_packets[MAX_ISO_PACKETS];
    unsigned char buffer[MAX_ISO_PACKETS * MAX_PACKET_SIZE];
};

// Function declarations
void print_streaming_control(struct uvc_streaming_control *ctrl);

int uvc_control_query(int fd, uint8_t request, uint8_t unit_id, 
                      uint8_t interface, uint8_t cs, void *data, uint16_t size);

int uvc_probe_commit(int fd, struct uvc_streaming_control *ctrl, int probe);

int set_interface_alt_setting(int fd, int interface, int alt_setting);

int claim_interface(int fd, int interface);

int release_interface(int fd, int interface);

int submit_iso_urb(int fd, struct usbdevfs_urb *urb, unsigned char *buffer, 
                    int endpoint, int num_packets, int packet_size);

struct usbdevfs_urb *reap_urb(int fd, int timeout_ms);

void process_video_data(unsigned char *data, int length, FILE *output);

#endif // UVC_CAMERA_H
