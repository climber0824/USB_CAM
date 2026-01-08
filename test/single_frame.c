#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

// --- UVC Definitions ---
#define VIDEO_CONTROL_INTERFACE   0
#define VIDEO_STREAMING_INTERFACE 1
#define VIDEO_ENDPOINT            0x81
#define NUM_URBS                  5
#define PACKETS_PER_URB           32
#define MAX_FRAME_SIZE            (512 * 1024) // 512KB for one JPEG frame

// UVC 1.0 Negotiation Structure (Packed to avoid C padding)
struct __attribute__((packed)) uvc_streaming_control {
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
};

// --- Global State ---
uint8_t *g_frame_buffer = NULL;
int g_frame_pos = 0;
int g_is_capturing = 0;

// --- Helper: SOI/EOI Parser ---
void process_mjpeg_payload(uint8_t *data, int len) {
    if (len <= 0) return;

    for (int i = 0; i < len - 1; i++) {
        // Start of Image (SOI)
        if (data[i] == 0xFF && data[i+1] == 0xD8) {
            g_frame_pos = 0;
            g_is_capturing = 1;
            printf("[Parser] SOI Found. Capturing...\n");
        }
        
        // End of Image (EOI)
        if (g_is_capturing && data[i] == 0xFF && data[i+1] == 0xD9) {
            if (g_frame_pos + 2 < MAX_FRAME_SIZE) {
                g_frame_buffer[g_frame_pos++] = data[i];
                g_frame_buffer[g_frame_pos++] = data[i+1];
            }
            printf("[Parser] EOI Found! Saving frame (%d bytes).\n", g_frame_pos);
            
            FILE *f = fopen("capture.jpg", "wb");
            if (f) {
                fwrite(g_frame_buffer, 1, g_frame_pos, f);
                fclose(f);
                printf("[System] Saved to capture.jpg. Success!\n");
                exit(0); // Exit after first successful frame
            }
            g_is_capturing = 0;
        }
    }

    if (g_is_capturing) {
        if (g_frame_pos + len < MAX_FRAME_SIZE) {
            memcpy(g_frame_buffer + g_frame_pos, data, len);
            g_frame_pos += len;
        } else {
            g_is_capturing = 0; // Overflow
        }
    }
}

// --- Main Logic ---
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: sudo %s /dev/bus/usb/00X/00Y\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("Open device");
        return 1;
    }

    // 1. Detach Kernel Driver (Crucial to avoid conflict with uvcvideo)
    struct usbdevfs_ioctl detach;
    detach.ifno = VIDEO_STREAMING_INTERFACE;
    detach.ioctl_code = USBDEVFS_DISCONNECT;
    detach.data = NULL;
    ioctl(fd, USBDEVFS_IOCTL, &detach);

    // 2. Claim Interfaces
    int intf = VIDEO_CONTROL_INTERFACE;
    ioctl(fd, USBDEVFS_CLAIMINTERFACE, &intf);
    intf = VIDEO_STREAMING_INTERFACE;
    ioctl(fd, USBDEVFS_CLAIMINTERFACE, &intf);

    // 3. UVC Negotiation (Probe & Commit)
    struct uvc_streaming_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bFormatIndex = 2; // Usually MJPEG
    ctrl.bFrameIndex = 1;  // Usually the first resolution
    ctrl.dwFrameInterval = 333333; // 30 FPS

    struct usbdevfs_ctrltransfer xfer;
    xfer.bRequestType = 0x21; // Host to Interface
    xfer.bRequest = 0x01;     // SET_CUR
    xfer.wValue = 0x0100;     // VS_PROBE_CONTROL
    xfer.wIndex = VIDEO_STREAMING_INTERFACE;
    xfer.wLength = 26;
    xfer.data = &ctrl;
    
    if (ioctl(fd, USBDEVFS_CONTROL, &xfer) < 0) perror("Probe SET");
    
    xfer.bRequestType = 0xA1; // Interface to Host
    xfer.bRequest = 0x81;     // GET_CUR
    if (ioctl(fd, USBDEVFS_CONTROL, &xfer) < 0) perror("Probe GET");

    printf("[UVC] Camera requested %u bytes/packet bandwidth.\n", ctrl.dwMaxPayloadTransferSize);

    xfer.bRequestType = 0x21;
    xfer.bRequest = 0x01;
    xfer.wValue = 0x0200;     // VS_COMMIT_CONTROL
    if (ioctl(fd, USBDEVFS_CONTROL, &xfer) < 0) perror("Commit SET");

    // 4. Set Alternate Setting (Enables the Endpoint)
    // For MJPEG, Alt 7 is typically the highest bandwidth (3072 bytes/packet)
    struct usbdevfs_setinterface set_intf;
    set_intf.interface = VIDEO_STREAMING_INTERFACE;
    set_intf.altsetting = 7; 
    if (ioctl(fd, USBDEVFS_SETINTERFACE, &set_intf) < 0) {
        perror("Set AltSetting 7 failed (trying Alt 1)");
        set_intf.altsetting = 1;
        ioctl(fd, USBDEVFS_SETINTERFACE, &set_intf);
    }

    // 5. Prepare Isochronous URBs
    g_frame_buffer = malloc(MAX_FRAME_SIZE);
    // Standard Max Packet size for Alt 7 is 3072. Using a safe max.
    // int packet_size = 3072; 
    int packet_size = ctrl.dwMaxPayloadTransferSize;
    
    printf("pack_size = %d\n", packet_size);

    // Safety check: if negotiation failed or returned 0
    if (packet_size <= 0 || packet_size > 3072) {
        printf("[Error] Invalid packet size: %d\n", packet_size);
        return 1;
    }

    printf("[System] Allocating URBs with packet_size: %d\n", packet_size);

    struct usbdevfs_urb *urbs[NUM_URBS];

    for (int i = 0; i < NUM_URBS; i++) {
        size_t urb_size = sizeof(struct usbdevfs_urb) + (PACKETS_PER_URB * sizeof(struct usbdevfs_iso_packet_desc));
        urbs[i] = malloc(urb_size);
        memset(urbs[i], 0, urb_size);

        urbs[i]->type = USBDEVFS_URB_TYPE_ISO;
        urbs[i]->endpoint = VIDEO_ENDPOINT;
        urbs[i]->buffer = malloc(packet_size * PACKETS_PER_URB);
        urbs[i]->buffer_length = packet_size * PACKETS_PER_URB;
        urbs[i]->number_of_packets = PACKETS_PER_URB;

        for (int j = 0; j < PACKETS_PER_URB; j++) {
            urbs[i]->iso_frame_desc[j].length = packet_size;
        }

        if (ioctl(fd, USBDEVFS_SUBMITURB, urbs[i]) < 0) {
            perror("Initial Submit URB");
        }
    }

    // 6. Streaming Loop
    printf("[System] Streaming started. Waiting for data...\n");
    while (1) {
        struct usbdevfs_urb *reaped;
        if (ioctl(fd, USBDEVFS_REAPURB, &reaped) == 0) {
            for (int p = 0; p < reaped->number_of_packets; p++) {
                struct usbdevfs_iso_packet_desc *d = &reaped->iso_frame_desc[p];
                if (d->status == 0 && d->actual_length > 0) {
                    uint8_t *ptr = (uint8_t*)reaped->buffer + (p * packet_size);
                    
                    // UVC Payload Header is usually ptr[0] bytes long
                    int hle = ptr[0];
                    if (hle > 0 && hle < d->actual_length) {
                        process_mjpeg_payload(ptr + hle, d->actual_length - hle);
                    }
                }
            }
            // Resubmit immediately
            ioctl(fd, USBDEVFS_SUBMITURB, reaped);
        }
    }

    return 0;
}
