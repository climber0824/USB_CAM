#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <jpeglib.h>

#include "uvc_camera.h"
#include "image_processing.h"
#include "mjpeg_parser.h"
#include "urb_manager.h"
#include "config.h"

#define NUM_URBS 4

// Global static buffers
static MJPEGParser g_parser;
static URGManager g_urb_mgr;
static Image g_current_frame;
static uint8_t g_jpeg_buffer[MAX_JPEG_SIZE];

// JPEG decoder (using fixed buffer)
static int jpeg_decode_to_image(const uint8_t *jpeg_data, int jpeg_size, Image *img) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int channels = cinfo.output_components;

    if (width > MAX_FRAME_WIDTH || height > MAX_FRAME_HEIGHT) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    image_init(img, width, height, channels);

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row = img->data + cinfo.output_scanline * img->step;
        jpeg_read_scanline(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return 0;
}

// Process frame callback
static void process_frame(Image *img, int frame_number, FILE *output) {
    if (!img || !img->valid) {
        printf("precess frame error\n");
        return;
    }

    image_adjust_brightness(img, 10);
    image_adjust_contrast(img, 1.2);

    if (output) {
        fwrite(img->data, 1, img->height * img->step, output);
    }
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s /dev/bus/usb/BBB/DDD\n", argv[0]);
        printf("Find your camera with: lsusb\n");
        return 1;
    }

    const char *output_file = argc > 2 ? argv[2] : "output.rgb";
    
    // Open USB device
    int fd_usb = open(argv[1], O_RDWR);
    if (fd_usb < 0) {
        perror("Failed to open USB device");
        printf("Try: sudo %s %s\n", argv[0], argv[1]);
        return 1;
    }

    // Detach kernel drivers
    struct usbdevfs_ioctl command;

    // Detach from control interface
    command.ifno = USB_VIDEO_CONTROL_INTERFACE;
    command.ioctl_code = USBDEVFS_DISCONNECT;
    command.data = NULL;
    ioctl(fd_usb, USBDEVFS_IOCTL, &command);

    // Detach from streaming interface
    command.ifno = USB_VIDEO_STREAMING_INTERFACE;
    ioctl(fd_usb, USBDEVFS_IOCTL, &command);
    
    printf("Opened camera: %s\n\n", argv[1]);
    
    // Claim video control interface
    if (claim_interface(fd_usb, USB_VIDEO_CONTROL_INTERFACE) < 0) {
        close(fd_usb);
        return 1;
    }
    
    // Claim video streaming interface
    if (claim_interface(fd_usb, USB_VIDEO_STREAMING_INTERFACE) < 0) {
        release_interface(fd_usb, USB_VIDEO_CONTROL_INTERFACE);
        close(fd_usb);
        return 1;
    }
    
    // Setup streaming control structure
    struct uvc_streaming_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bmHint = 1;
    ctrl.bFormatIndex = 1;
    ctrl.bFrameIndex = 1;
    ctrl.dwFrameInterval = 333333;  // 30 fps
    ctrl.dwMaxVideoFrameSize = 614400;
    ctrl.dwMaxPayloadTransferSize = 3072;
    
    // Probe - negotiate parameters
    printf("Probing camera settings...\n");
    if (uvc_probe_commit(fd_usb, &ctrl, 1) < 0) {
        printf("Probe failed\n");
        goto cleanup;
    }
    print_streaming_control(&ctrl);
    
    // Commit - finalize parameters
    printf("\nCommitting settings...\n");
    if (uvc_probe_commit(fd_usb, &ctrl, 0) < 0) {
        printf("Commit failed\n");
        goto cleanup;
    }
    
    printf("\n=== Camera Negotiated Settings ===\n");
    printf("Max Payload Transfer Size: %u bytes\n", ctrl.dwMaxPayloadTransferSize);
    printf("Max Video Frame Size: %u bytes\n", ctrl.dwMaxVideoFrameSize);
    printf("Frame Interval: %u (%.2f fps)\n", ctrl.dwFrameInterval, 
           10000000.0 / ctrl.dwFrameInterval);
    printf("================================\n\n");

    // Use this value directly
    int packet_size = ctrl.dwMaxPayloadTransferSize;
    if (packet_size > 3072) {
        packet_size = 3072;  // Limit to safe maximum
    }   

    printf("\nFinding suitable alternate setting...\n");

    // Try alternate settings from highest to lowest (higher -> more bandwidth)
    int alt_setting = -1;
    for (int alt = 11; alt >= 1; alt--) {
        printf("Trying alternate setting %d...\n", alt);
        if (set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, alt) == 0) {
            alt_setting = alt;
            printf("Successfully set alternate setting %d\n", alt);
            break;
        }
    }

    if (alt_setting < 0) {
        printf("ERROR: Could not set any alternate setting!\n");
        goto cleanup;
    }
    
    printf("\nEnabling video streaming...\n");

    // Initialize static structures
    mjpeg_parser_init(&g_parser);
    urb_manager_init(&g_urb_mgr);
    image_init(&g_current_frame, 0, 0, 0);
    
    // Open output file for video data
    FILE *output = fopen("output_file", "wb");
    if (!output) {
        perror("Failed to open output file");
        goto cleanup;
    }
    
    printf("\nCapturing video stream (Ctrl+C to stop)...\n");
    printf("Output: %s\n", output_file);
    printf("Progress: ");
    fflush(stdout);
    
    // Submit multiple URBs for continuous streaming
    struct usbdevfs_urb *urbs[NUM_URBS];
    unsigned char *buffers[NUM_URBS];
    int endpoint = 0x81;
    int num_packets = 16;
    int max_buffer_size = num_packets * 3072;

    if (alt_setting <= 3) {
        packet_size = 512;
        num_packets = 8;
    }
    else if (alt_setting <= 6) {
        packet_size = 1024;
        num_packets = 12;
    }

    // Calculate size needed for each URB
    size_t urb_size = sizeof(struct usbdevfs_urb) + 
                        num_packets * sizeof(struct usbdevfs_iso_packet_desc);
    
    printf("\nAllocating URBs:\n");
    printf("  URB structure size: %zu bytes\n", urb_size);
    printf("  Buffer size: %d bytes\n", packet_size * num_packets);
    printf("  packet_size: %d bytes\n", packet_size);    

    // Make sure total size isn't too larger (about ~100KB per URB)
    while (packet_size * num_packets > max_buffer_size && num_packets > 1) {
        num_packets--;
    }

    printf("\nUsing: alt=%d, %d packets of %d bytes (total: %d bytes per URB\n",
            alt_setting, num_packets, packet_size, packet_size * num_packets);

    // Submit initial URBs
    for (int i = 0; i < NUM_URBS; i++) {
        // Allocate URB with space for iso_frame_desc array
        urbs[i] = (struct usbdevfs_urb *)malloc(urb_size);
        if (!urbs[i]) {
            perror("Failed to allocate URB");
            goto cleanup;
        }

        // Allocate seperate buffer for data
        buffers[i] = (unsigned char *)malloc(packet_size * num_packets);
        if (!buffers[i]) {
            perror("Failed to allocate buffer");
            free(urbs[i]);
            goto cleanup;
        }
            

        // Submit URB
        if (submit_iso_urb(fd_usb, &g_urb_mgr.urbs[i], buffers[i], endpoint, num_packets, packet_size) < 0) {
            printf("Failed to submit initial URB %d\n", i);
            free(urbs[i]);
            free(buffers[i]);
        }
        else {
            g_urb_mgr.num_active++;
            printf("Successfully sumbit URB %d\n", i);
        }
    }

    if (g_urb_mgr.num_active == 0) {
        printf("Failed to submit any URBs\n");
        fclose(output);
        goto cleanup;
    }
 
    // Capture loop - capture 300 frames then stop
    int frame_count = 0;
    int max_frames = 300;
    printf("\nCapturing %d frames...\n", max_frames);

    while (frame_count < max_frames) {
        struct usbdevfs_urb *completed_urb = reap_urb(fd_usb, 1000);
        
        if (completed_urb) {
            // Process the received data
            int offset = 0;
            for (int i = 0; i < completed_urb->number_of_packets; i++) {
                int actual_length = completed_urb->iso_frame_desc[i].actual_length;
                if (actual_length > 2) {
                    uint8_t *data = completed_urb->buffer + offset;
                    uint8_t header_len = data[0];
                    
                    if (actual > header_len) {
                        mjpeg_parser_add_data(&g_parser, data + header_len, 
                                             actual - header_len);
                    }    
                }
                offset += completed_urb->iso_frame_desc[i].length;
            }

            // Try to extract frames
            int frame_size;
            while (mjpeg_parser_get_frame(&g_parser, g_jpeg_buffer, &frame_size) > 0) {
                // Decode JPEG to image
                if (jpeg_decode_to_image(g_jpeg_buffer, frame_size, &g_current_frame) == 0) {
                    // Process frame
                    process_frame(&g_current_frame, frame_count, output);

                    frame_count++;
                    if (frame_count % 10 == 0) {
                        printf("\rFrames: %d/%d", frame_count, MAX_FRAMES);
                        fflush(stdout);
                    }

                    if (frame_count >= MAX_FRAMES) break;
                }
            }
            
            // Resubmit the URB for continuous streaming
            if (frame_count < max_frames) {
                if (ioctl(fd_usb, USBDEVFS_SUBMITURB, completed_urb) < 0) {
                    perror("Failed to resubmit URB");
                    break;
                }
            }
        }
    }

    
    printf("\n\nCaptured approximately %d frames\n", frame_count);
    printf("Output saved to: %s\n", output_file);

    // Convert to MP4 using ffmpeg
    if (g_current_frame.valid) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd),
                "ffmpeg -y -f rawvideo -pixel_format rgb24 -video_size %dx%d "
                "-framerate %d -i %s -c:v libx264 -preset fast -crf 23 "
                "-pix_fmt yuv420p output.mp4",
                g_current_frame.width, g_current_frame.height, 
                DEFAULT_FPS, output_file);
        printf("\nConverting to MP4...\n");
        system(cmd);
        printf("MP4 created: output.mp4\n");
    }

    fclose(output);
    
    // Cancel and free URBs
    for (int i = 0; i < NUM_URBS; i++) {
        if (urbs[i]) {
            ioctl(fd_usb, USBDEVFS_DISCARDURB, urbs[i]);
            free(buffers[i]);
            free(urbs[i]);
        }
    }

    // Disable streaming
    set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, 0);
    
cleanup:
    set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, 0);
    release_interface(fd_usb, USB_VIDEO_STREAMING_INTERFACE);
    release_interface(fd_usb, USB_VIDEO_CONTROL_INTERFACE);
    close(fd_usb);
    
    printf("Done!\n");
    printf("\nNote: Output is raw video data (likely MJPEG or YUV).\n");
    printf("You'll need to parse the format based on your camera specs.\n");
    
    return 0;
}
#include "uvc_camera.h"
