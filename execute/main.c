#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <setjmp.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>
#include <jpeglib.h>

#define VIDEO_STREAMING_INTERFACE 1
#define VIDEO_ENDPOINT            0x81
#define NUM_URBS                  5
#define PACKETS_PER_URB           32
#define MAX_FRAME_SIZE            (1024 * 1024) 
#define TARGET_FRAMES             300

// --- Global State ---
uint8_t *g_jpeg_buffer = NULL;
int g_jpeg_pos = 0;
int g_frames_processed = 0;
int g_last_fid = -1;
FILE *g_ffmpeg_pipe = NULL;

// Error handling for libjpeg
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};
void my_error_exit(j_common_ptr cinfo) {
    struct my_error_mgr *myerr = (struct my_error_mgr *)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

struct __attribute__((packed)) uvc_streaming_control {
    uint16_t bmHint; uint8_t bFormatIndex; uint8_t bFrameIndex;
    uint32_t dwFrameInterval; uint16_t wKeyFrameRate; uint16_t wPFrameRate;
    uint16_t wCompQuality; uint16_t wCompWindowSize; uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize; uint32_t dwMaxPayloadTransferSize;
    uint32_t dwClockFrequency; uint8_t bmFramingInfo; uint8_t bPreferedVersion;
    uint8_t bMinVersion; uint8_t bMaxVersion;
};

void decode_and_encode() {
    if (g_jpeg_pos < 100) return; // Ignore tiny fragments

    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        // If we get here, libjpeg found a corrupt frame
        jpeg_destroy_decompress(&cinfo);
        return; 
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, g_jpeg_buffer, g_jpeg_pos);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return;
    }

    jpeg_start_decompress(&cinfo);
    
    if (!g_ffmpeg_pipe) {
        char cmd[512];
        sprintf(cmd, "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size %dx%d "
                     "-framerate 30 -i - -c:v libx264 -pix_fmt yuv420p output.mp4", 
                     cinfo.output_width, cinfo.output_height);
        g_ffmpeg_pipe = popen(cmd, "w");
    }

    uint8_t *row = malloc(cinfo.output_width * 3);
    while (cinfo.output_scanline < cinfo.output_height) {
        jpeg_read_scanlines(&cinfo, &row, 1);
        fwrite(row, 1, cinfo.output_width * 3, g_ffmpeg_pipe);
    }
    free(row);

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    g_frames_processed++;
    printf("\r[Capture] Frame %d/300  ", g_frames_processed);
    fflush(stdout);

    if (g_frames_processed >= TARGET_FRAMES) {
        printf("\n[Done] 300 frames captured to output.mp4\n");
        pclose(g_ffmpeg_pipe);
        exit(0);
    }
}

void handle_packet(uint8_t *ptr, int actual_len) {
    if (actual_len < 2) return;

    uint8_t hle = ptr[0];      // Header Length
    uint8_t bitfield = ptr[1]; // Bitfield (FID, EOF, etc.)

    if (hle > actual_len || hle < 2) return;

    int fid = bitfield & 0x01;
    int eof = (bitfield >> 1) & 0x01;

    // 1. If FID toggled, we definitely missed the EOF of the last frame or started a new one
    if (g_last_fid != -1 && fid != g_last_fid) {
        decode_and_encode();
        g_jpeg_pos = 0;
    }
    g_last_fid = fid;

    // 2. Append payload data (skipping the header)
    int payload_len = actual_len - hle;
    if (payload_len > 0 && (g_jpeg_pos + payload_len < MAX_FRAME_SIZE)) {
        memcpy(g_jpeg_buffer + g_jpeg_pos, ptr + hle, payload_len);
        g_jpeg_pos += payload_len;
    }

    // 3. If EOF bit is set, this frame is complete
    if (eof) {
        decode_and_encode();
        g_jpeg_pos = 0;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    int fd = open(argv[1], O_RDWR);
    
    // Detach and Claim
    struct usbdevfs_ioctl detach = { .ifno = VIDEO_STREAMING_INTERFACE, .ioctl_code = USBDEVFS_DISCONNECT };
    ioctl(fd, USBDEVFS_IOCTL, &detach);
    int intf = VIDEO_STREAMING_INTERFACE;
    ioctl(fd, USBDEVFS_CLAIMINTERFACE, &intf);

    // Negotiation
    struct uvc_streaming_control ctrl = { .bFormatIndex = 2, .bFrameIndex = 1, .dwFrameInterval = 333333 };
    struct usbdevfs_ctrltransfer xfer = { .bRequestType = 0x21, .bRequest = 0x01, .wValue = 0x0100, 
                                          .wIndex = VIDEO_STREAMING_INTERFACE, .wLength = 26, .data = &ctrl };
    ioctl(fd, USBDEVFS_CONTROL, &xfer);
    xfer.bRequestType = 0xA1; xfer.bRequest = 0x81;
    ioctl(fd, USBDEVFS_CONTROL, &xfer);
    
    int packet_size = ctrl.dwMaxPayloadTransferSize;
    struct usbdevfs_setinterface set_intf = { .interface = VIDEO_STREAMING_INTERFACE, .altsetting = 7 };
    ioctl(fd, USBDEVFS_SETINTERFACE, &set_intf);

    xfer.bRequestType = 0x21; xfer.bRequest = 0x01; xfer.wValue = 0x0200; xfer.data = &ctrl;
    ioctl(fd, USBDEVFS_CONTROL, &xfer);

    g_jpeg_buffer = malloc(MAX_FRAME_SIZE);

    // Submit URBs
    for (int i = 0; i < NUM_URBS; i++) {
        size_t sz = sizeof(struct usbdevfs_urb) + (PACKETS_PER_URB * sizeof(struct usbdevfs_iso_packet_desc));
        struct usbdevfs_urb *urb = malloc(sz);
        memset(urb, 0, sz);
        urb->type = USBDEVFS_URB_TYPE_ISO;
        urb->endpoint = VIDEO_ENDPOINT;
        urb->buffer = malloc(packet_size * PACKETS_PER_URB);
        urb->buffer_length = packet_size * PACKETS_PER_URB;
        urb->number_of_packets = PACKETS_PER_URB;
        for (int j = 0; j < PACKETS_PER_URB; j++) urb->iso_frame_desc[j].length = packet_size;
        ioctl(fd, USBDEVFS_SUBMITURB, urb);
    }

    while (1) {
        struct usbdevfs_urb *reaped;
        if (ioctl(fd, USBDEVFS_REAPURB, &reaped) == 0) {
            for (int p = 0; p < reaped->number_of_packets; p++) {
                struct usbdevfs_iso_packet_desc *d = &reaped->iso_frame_desc[p];
                if (d->status == 0 && d->actual_length > 0) {
                    handle_packet((uint8_t*)reaped->buffer + (p * packet_size), d->actual_length);
                }
            }
            ioctl(fd, USBDEVFS_SUBMITURB, reaped);
        }
    }
    return 0;
}
