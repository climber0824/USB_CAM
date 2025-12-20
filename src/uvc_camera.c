#include "uvc_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <errno.h>

void print_streaming_control(struct uvc_streaming_control *ctrl) {
    printf("Streaming Control:\n");
    printf("  bmHint: 0x%04x\n", ctrl->bmHint);
    printf("  bFormatIndex: %d\n", ctrl->bFormatIndex);
    printf("  bFrameIndex: %d\n", ctrl->bFrameIndex);
    printf("  dwFrameInterval: %u (%.2f fps)\n", 
           ctrl->dwFrameInterval, 
           10000000.0 / ctrl->dwFrameInterval);
    printf("  dwMaxVideoFrameSize: %u bytes\n", ctrl->dwMaxVideoFrameSize);
    printf("  dwMaxPayloadTransferSize: %u bytes\n", ctrl->dwMaxPayloadTransferSize);
}

int uvc_control_query(int fd, uint8_t request, uint8_t unit_id, 
                      uint8_t interface, uint8_t cs, void *data, uint16_t size) {
    struct usbdevfs_ctrltransfer ctrl;
    
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = (request & 0x80) ? 
                        (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) :
                        (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE);
    ctrl.bRequest = request;
    ctrl.wValue = cs << 8;
    ctrl.wIndex = (unit_id << 8) | interface;
    ctrl.wLength = size;
    ctrl.timeout = 5000;
    ctrl.data = data;
    
    if (ioctl(fd, USBDEVFS_CONTROL, &ctrl) < 0) {
        perror("UVC control query failed");
        return -1;
    }
    
    return 0;
}

int uvc_probe_commit(int fd, struct uvc_streaming_control *ctrl, int probe) {
    int ret;
    uint8_t request = probe ? UVC_VS_PROBE_CONTROL : UVC_VS_COMMIT_CONTROL;
    
    // First, send probe/commit request (SET_CUR)
    ret = uvc_control_query(fd, UVC_SET_CUR, 0, USB_VIDEO_STREAMING_INTERFACE,
                           request, ctrl, sizeof(*ctrl));
    if (ret < 0) {
        printf("Failed to set %s\n", probe ? "probe" : "commit");
        return ret;
    }
    
    // Then get the result (GET_CUR)
    ret = uvc_control_query(fd, UVC_GET_CUR, 0, USB_VIDEO_STREAMING_INTERFACE,
                           request, ctrl, sizeof(*ctrl));
    if (ret < 0) {
        printf("Failed to get %s result\n", probe ? "probe" : "commit");
        return ret;
    }
    
    return 0;
}

int set_interface_alt_setting(int fd, int interface, int alt_setting) {
    struct usbdevfs_setinterface setintf;
    
    setintf.interface = interface;
    setintf.altsetting = alt_setting;
    
    if (ioctl(fd, USBDEVFS_SETINTERFACE, &setintf) < 0) {
        perror("Failed to set interface alternate setting");
        return -1;
    }
    
    printf("Set interface %d to alternate setting %d\n", interface, alt_setting);
    return 0;
}

int claim_interface(int fd, int interface) {
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface) < 0) {
        perror("Failed to claim interface");
        return -1;
    }
    printf("Claimed interface %d\n", interface);
    return 0;
}

int release_interface(int fd, int interface) {
    if (ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interface) < 0) {
        perror("Failed to release interface");
        return -1;
    }
    return 0;
}

int submit_iso_urb(int fd, struct usbdevfs_urb *urb, unsigned char *buffer, 
                    int endpoint, int num_packets, int packet_size) {
    // Clear the URB structure
    memset(urb, 0, sizeof(struct usbdevfs_urb));
    
    urb->type = USBDEVFS_URB_TYPE_ISO;
    urb->endpoint = endpoint;
    urb->buffer = buffer;
    urb->buffer_length = num_packets * packet_size;
    urb->number_of_packets = num_packets;

    // Set up iso packets
    for (int i = 0; i < num_packets; i++) {
        urb->iso_frame_desc[i].length = packet_size;
        urb->iso_frame_desc[i].actual_length = 0;
        urb->iso_frame_desc[i].status = 0;
    }

    printf("Submitting URB: endpoint=0x%02x, packets=%d, size=%d, total=%d\n",
           endpoint, num_packets, packet_size, num_packets * packet_size);

    if (ioctl(fd, USBDEVFS_SUBMITURB, urb) < 0) {
        perror("Failed to submit URB");
        return -1;
    }
    
    return 0;
}

struct usbdevfs_urb *reap_urb(int fd, int timeout_ms) {
    struct usbdevfs_urb *urb;
    
    if (ioctl(fd, USBDEVFS_REAPURB, &urb) < 0) {
        if (errno == EAGAIN || errno == ENODEV) {
            return NULL;
        }
        perror("Failed to reap URB");
        return NULL;
    }
    
    return urb;
}

void process_video_data(unsigned char *data, int length, FILE *output) {
    // UVC payload header parsing
    if (length < 2) return;
    
    uint8_t header_len = data[0];
    uint8_t header_info = data[1];
    
    // Check if this is frame data (not just header)
    if (length > header_len) {
        // Write frame data to file (skip UVC header)
        fwrite(data + header_len, 1, length - header_len, output);
        
        // Check End of Frame bit
        if (header_info & 0x02) {
            printf(".");
            fflush(stdout);
        }
    }
}
