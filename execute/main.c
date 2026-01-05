#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <jpeglib.h>
#include <errno.h>
#include <unistd.h>

#include "uvc_camera.h"
#include "image_processing.h"
#include "mjpeg_parser.h"
#include "urb_manager.h"
#include "config.h"

#define NUM_URBS 4

// Global static buffers
static MJPEGParser g_parser;
static URBManager g_urb_mgr;
static Image g_current_frame;
static uint8_t g_jpeg_buffer[MAX_JPEG_SIZE];

// JPEG decoder (using fixed buffer)
static int jpeg_decode_to_image(const uint8_t *jpeg_data, int jpeg_size, Image *img) {
    if (!jpeg_data || jpeg_size < 0 || !img) {
        printf("jpeg decode error");
        return -1;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
    int ret = jpeg_read_header(&cinfo, TRUE);
    if (ret != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    if (!jpeg_start_decompress(&cinfo)) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    int width = cinfo.output_width;
    int height = cinfo.output_height;
    int channels = cinfo.output_components;

    if (width > MAX_FRAME_WIDTH || height > MAX_FRAME_HEIGHT || 
        width <= 0 || height <= 0 || channels != 3)  {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    image_init(img, width, height, channels);

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char *row = img->data + cinfo.output_scanline * img->step;
        if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) {
            jpeg_destroy_decompress(&cinfo);
            img->valid = 0;
            return -1;
        }
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

void debug_print_hex(const char *label, const uint8_t *data, int len) {
    printf("%s (%d bytes): ", label, len);
    int print_len = len > 32 ? 32 : len;  // Print first 32 bytes
    for (int i = 0; i < print_len; i++) {
        printf("%02X ", data[i]);
        if (i == 15) printf("\n                ");
    }
    if (len > 32) printf("...");
    printf("\n");
}

int start_video_stream(int fd, int interface) {
    struct usbdevfs_ctrltransfer ctrl;
    uint8_t data = 0x01;  // Stream ON

    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.bRequestType = 0x21;  // Host to device, class, interface
    ctrl.bRequest = 0x0B;       // SET_INTERFACE (or try 0x01 for SET_CUR)
    ctrl.wValue = 0x0100;       // Video streaming ON
    ctrl.wIndex = interface;    // Streaming interface
    ctrl.wLength = 1;
    ctrl.timeout = 5000;
    ctrl.data = &data;

    printf("Sending VIDEO STREAM ON command...\n");
    int ret = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
    if (ret < 0) {
        printf("  Stream ON failed: %s\n", strerror(errno));

        // Try alternative method
        ctrl.bRequest = 0x01;  // SET_CUR
        ctrl.wValue = 0x0200;  // VS_COMMIT_CONTROL
        ret = ioctl(fd, USBDEVFS_CONTROL, &ctrl);
        if (ret < 0) {
            printf("  Alternative method also failed\n");
            return -1;
        }
    }

    printf("  ✓ Stream ON command sent\n");
    return 0;
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
    ctrl.bFormatIndex = 2;
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
    
    start_video_stream(fd_usb, USB_VIDEO_STREAMING_INTERFACE);

    usleep(100000);

    // Use this value directly
    int packet_size = ctrl.dwMaxPayloadTransferSize;
    if (packet_size > 3072) {
        packet_size = 3072;  // Limit to safe maximum
    }   

    //printf("\nFinding suitable alternate setting...\n");

    // Try alternate settings from highest to lowest (higher -> more bandwidth)
    //int alt_setting = -1;
    //for (int alt = 11; alt >= 1; alt--) {
    //    printf("Trying alternate setting %d...\n", alt);
    //    if (set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, alt) == 0) {
    //        alt_setting = alt;
    //        printf("Successfully set alternate setting %d\n", alt);
    //        break;
    //    }
    //}

    //if (alt_setting < 0) {
    //    printf("ERROR: Could not set any alternate setting!\n");
    //    goto cleanup;
    //}

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
    
    // NOW find and set the working alternate setting
    printf("Finding working alternate setting...\n");

    int working_alt = -1;
    int working_packet_size = 0;

    // Try different alternate settings with TEST URBs
    for (int alt = 7; alt >= 1; alt--) {
        printf("Testing alternate setting %d...\n", alt);

        if (set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, alt) < 0) {
            printf("  ✗ Failed to set alt %d\n", alt);
            continue;
        }

        // Determine packet size based on alt setting
        int test_packet_size;
        if (alt >= 7) {
            test_packet_size = 1024;
        } else if (alt >= 4) {
            test_packet_size = 512;
        } else {
            test_packet_size = 256;
        }

        // Test with a single URB
        URBBuffer test_urb;
        memset(&test_urb, 0, sizeof(URBBuffer));

        test_urb.urb.type = USBDEVFS_URB_TYPE_ISO;
        test_urb.urb.endpoint = 0x81;
        test_urb.urb.buffer = test_urb.buffer;
        test_urb.urb.buffer_length = 8 * test_packet_size;
        test_urb.urb.number_of_packets = 8;

        for (int j = 0; j < 8; j++) {
            test_urb.urb.iso_frame_desc[j].length = test_packet_size;
        }

        if (ioctl(fd_usb, USBDEVFS_SUBMITURB, &test_urb.urb) == 0) {
            printf("  ✓ URB submitted\n");

            // Wait for data
            usleep(100000);  // 100ms - longer wait

            // Try to reap it
            struct usbdevfs_urb *completed = NULL;
            int reap_ret = ioctl(fd_usb, USBDEVFS_REAPURBNDELAY, &completed);

            if (reap_ret == 0 && completed) {
                // Check if we got real data
                int has_data = 0;
                int total_data = 0;

                for (int j = 0; j < completed->number_of_packets; j++) {
                    int len = completed->iso_frame_desc[j].actual_length;
                    total_data += len;

                    if (len > 20) {  // More than just header
                        has_data = 1;
                        printf("  ✓ Packet %d: %d bytes\n", j, len);
                    }
                }

                printf("  Total data received: %d bytes\n", total_data);

                if (has_data) {
                    working_alt = alt;
                    working_packet_size = test_packet_size;
                    printf("  ✓✓ Alt %d WORKS with %d byte packets!\n", alt, test_packet_size);
                    break;
                } else {
                    printf("  ✗ Only headers (total: %d bytes)\n", total_data);
                }
            } else {
                printf("  ✗ Reap failed or timeout\n");
                ioctl(fd_usb, USBDEVFS_DISCARDURB, &test_urb.urb);
            }
        } else {
            printf("  ✗ Submit failed: %s\n", strerror(errno));
        }

        usleep(50000);  // 50ms between tests
    }

    if (working_alt < 0) {
        printf("\nERROR: No working alternate setting found!\n");
        printf("This might mean:\n");
        printf("1. Camera doesn't support MJPEG format 2\n");
        printf("2. USB bandwidth issues\n");
        printf("3. Camera needs different initialization\n");

        // Try one more thing: check if camera needs explicit start
        printf("\nTrying to read camera status...\n");
        uint8_t status[4];
        struct usbdevfs_ctrltransfer ctrl_read;
        memset(&ctrl_read, 0, sizeof(ctrl_read));
        ctrl_read.bRequestType = 0xA1;  // Device to host, class, interface
        ctrl_read.bRequest = 0x87;  // GET_CUR
        ctrl_read.wValue = 0x0200;  // VS_COMMIT_CONTROL
        ctrl_read.wIndex = 0x0001;  // Streaming interface
        ctrl_read.wLength = 4;
        ctrl_read.timeout = 5000;
        ctrl_read.data = status;

        if (ioctl(fd_usb, USBDEVFS_CONTROL, &ctrl_read) >= 0) {
            printf("Camera status: %02X %02X %02X %02X\n",
                   status[0], status[1], status[2], status[3]);
        }

        goto cleanup;
    }

    printf("\n✓✓✓ Using alt setting %d with %d byte packets ✓✓✓\n\n",
           working_alt, working_packet_size);

    // Re-set the working alternate setting to be sure
    set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, working_alt);



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
    // struct usbdevfs_urb *urbs[NUM_URBS];
    URBBuffer *urbs[NUM_URBS];
    // unsigned char *buffers[NUM_URBS];
    unsigned char *temp_buffers[NUM_URBS];
    int endpoint = 0x81;
    int num_packets = 16;
    packet_size = working_packet_size;
    int max_buffer_size = num_packets * 3072;

    if (working_alt <= 3) {
        packet_size = 256;
        num_packets = 4;
    }
    else if (working_alt <= 6) {
        packet_size = 512;
        num_packets = 8;
    }
    else {
        packet_size = 1024;
        num_packets = 12;
    }

    // Adjust packets based on size
    if (packet_size == 256) {
        num_packets = 8;
    } else if (packet_size == 512) {
        num_packets = 12;
    } else {
        num_packets = 16;
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
            working_alt, num_packets, packet_size, packet_size * num_packets);

    // Submit initial URBs
   // for (int i = 0; i < NUM_URBS; i++) {
   //     // Allocate URB with space for iso_frame_desc array
   //     urbs[i] = (struct usbdevfs_urb *)malloc(urb_size);
   //     if (!urbs[i]) {
   //         perror("Failed to allocate URB");
   //         goto cleanup;
   //     }

   //     // Allocate seperate buffer for data
   //     buffers[i] = (unsigned char *)malloc(packet_size * num_packets);
   //     if (!buffers[i]) {
   //         perror("Failed to allocate buffer");
   //         free(urbs[i]);
   //         goto cleanup;
   //     }
   //         

   //     // Submit URB
   //     if (urb_submit(fd_usb, &g_urb_mgr.urbs[i], endpoint, num_packets, packet_size) < 0) {
   //         printf("Failed to submit initial URB %d\n", i);
   //         free(urbs[i]);
   //         free(buffers[i]);
   //     }
   //     else {
   //         g_urb_mgr.num_active++;
   //         printf("Successfully sumbit URB %d\n", i);
   //     }
   // }

   // if (g_urb_mgr.num_active == 0) {
   //     printf("Failed to submit any URBs\n");
   //     fclose(output);
   //     goto cleanup;
   // }


   // test section
   // IMPORTANT: Use smaller packet sizes to start
    printf("\nFinding working configuration...\n");


    // Try different alternate settings with TEST URBs
    for (int alt = 7; alt >= 1; alt--) {
        printf("Testing alternate setting %d...\n", alt);

        if (set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, alt) < 0) {
            continue;
        }

        // Determine packet size based on alt setting
        int test_packet_size;
        if (alt >= 7) {
            test_packet_size = 1024;  // Start with 1KB, not 3KB
        } else if (alt >= 4) {
            test_packet_size = 512;
        } else {
            test_packet_size = 256;
        }

        // Test with a single URB
        URBBuffer test_urb;
        memset(&test_urb, 0, sizeof(URBBuffer));

        test_urb.urb.type = USBDEVFS_URB_TYPE_ISO;
        test_urb.urb.endpoint = 0x81;
        test_urb.urb.buffer = test_urb.buffer;
        test_urb.urb.buffer_length = 8 * test_packet_size;  // 8 packets
        test_urb.urb.number_of_packets = 8;

        for (int j = 0; j < 8; j++) {
            test_urb.urb.iso_frame_desc[j].length = test_packet_size;
        }

        if (ioctl(fd_usb, USBDEVFS_SUBMITURB, &test_urb.urb) == 0) {
            printf("  ✓ Alt %d with %d byte packets: URB submitted\n", alt, test_packet_size);

            // Wait briefly for data
            usleep(50000);  // 50ms

            // Try to reap it
            struct usbdevfs_urb *completed;
            if (ioctl(fd_usb, USBDEVFS_REAPURBNDELAY, &completed) == 0) {
                // Check if we got real data (not just headers)
                int has_data = 0;
                for (int j = 0; j < completed->number_of_packets; j++) {
                    if (completed->iso_frame_desc[j].actual_length > 20) {
                        has_data = 1;
                        printf("  ✓ Packet %d has %d bytes (good!)\n",
                               j, completed->iso_frame_desc[j].actual_length);
                        break;
                    }
                }

                if (has_data) {
                    working_alt = alt;
                    working_packet_size = test_packet_size;
                    printf("  ✓✓ Alt setting %d WORKS with %d byte packets!\n",
                           alt, test_packet_size);
                    break;
                } else {
                    printf("  ✗ Alt %d: Only headers, no payload\n", alt);
                    ioctl(fd_usb, USBDEVFS_DISCARDURB, &test_urb.urb);
                }
            } else {
                // Couldn't reap, cancel it
                ioctl(fd_usb, USBDEVFS_DISCARDURB, &test_urb.urb);
                printf("  ✗ Alt %d: Couldn't reap URB\n", alt);
            }
        } else {
            printf("  ✗ Alt %d: Submit failed - %s\n", alt, strerror(errno));
        }

        // Small delay between tests
        usleep(10000);
    }

    if (working_alt < 0) {
        printf("ERROR: No working alternate setting found!\n");
        printf("Camera may not support MJPEG or format mismatch\n");
        goto cleanup;
    }

    printf("\n✓✓✓ Using alt setting %d with %d byte packets ✓✓✓\n\n",
           working_alt, working_packet_size);
    //----------test section end-----------//


    // new ver.
    int successful_urbs = 0;

    for (int i = 0; i < NUM_URBS; i++) {
        // Allocate URB structure with extra space for iso packets
        urbs[i] = (URBBuffer*)malloc(sizeof(URBBuffer));
        if (!urbs[i]) {
            printf("Failed to allocate URB %d\n", i);
            continue;
        }
        
        memset(urbs[i], 0, sizeof(URBBuffer));
        
        // Set up URB
        urbs[i]->urb.type = USBDEVFS_URB_TYPE_ISO;
        urbs[i]->urb.endpoint = endpoint;
        urbs[i]->urb.buffer = urbs[i]->buffer;
        urbs[i]->urb.buffer_length = num_packets * packet_size;
        urbs[i]->urb.number_of_packets = num_packets;
        
        // Set up iso packet descriptors
        for (int j = 0; j < num_packets; j++) {
            urbs[i]->urb.iso_frame_desc[j].length = packet_size;
            urbs[i]->urb.iso_frame_desc[j].actual_length = 0;
            urbs[i]->urb.iso_frame_desc[j].status = 0;
        }
        
        // Submit URB
        if (ioctl(fd_usb, USBDEVFS_SUBMITURB, &urbs[i]->urb) == 0) {
            successful_urbs++;
            printf("✓ URB %d submitted\n", i);
        } else {
            printf("✗ URB %d failed: %s\n", i, strerror(errno));
            free(urbs[i]);
            urbs[i] = NULL;
        }
    }

    if (successful_urbs == 0) {
        printf("ERROR: Could not submit any URBs!\n");
        goto cleanup;
    }

    printf("\nStreaming with %d URBs...\n", successful_urbs);
    printf("Progress: ");
    fflush(stdout);
    // new ver. end

    // Capture loop - capture 300 frames then stop
    int frame_count = 0;
    int max_frames = 300;
    printf("\nCapturing %d frames...\n", max_frames);

    // old ver.
   // while (frame_count < max_frames) {
   //     struct usbdevfs_urb *completed_urb = reap_urb(fd_usb, 1000);
   //     
   //     if (completed_urb) {
   //         // Process the received data
   //         int offset = 0;
   //         for (int i = 0; i < completed_urb->number_of_packets; i++) {
   //             int actual_length = completed_urb->iso_frame_desc[i].actual_length;
   //             if (actual_length > 2) {
   //                 uint8_t *data = completed_urb->buffer + offset;
   //                 uint8_t header_len = data[0];
   //                 
   //                 if (actual_length > header_len) {
   //                     mjpeg_parser_add_data(&g_parser, data + header_len, 
   //                                          actual_length - header_len);
   //                 }    
   //             }
   //             offset += completed_urb->iso_frame_desc[i].length;
   //         }

   //         // Try to extract frames
   //         int frame_size;
   //         while (mjpeg_parser_get_frame(&g_parser, g_jpeg_buffer, &frame_size) > 0) {
   //             // Decode JPEG to image
   //             if (jpeg_decode_to_image(g_jpeg_buffer, frame_size, &g_current_frame) == 0) {
   //                 // Process frame
   //                 process_frame(&g_current_frame, frame_count, output);

   //                 frame_count++;
   //                 if (frame_count % 10 == 0) {
   //                     printf("\rFrames: %d/%d", frame_count, MAX_FRAMES);
   //                     fflush(stdout);
   //                 }

   //                 if (frame_count >= MAX_FRAMES) break;
   //             }
   //         }
   //         
   //         // Resubmit the URB for continuous streaming
   //         if (frame_count < max_frames) {
   //             if (ioctl(fd_usb, USBDEVFS_SUBMITURB, completed_urb) < 0) {
   //                 perror("Failed to resubmit URB");
   //                 break;
   //             }
   //         }
   //     }
   // }

   // new ver.
    while (frame_count < max_frames) {
        printf("About to reap URB...\n");
        fflush(stdout);
        struct usbdevfs_urb *completed = NULL;
        
        // Reap URB with timeout
        int ret = ioctl(fd_usb, USBDEVFS_REAPURB, &completed);
        printf("Reap returned: %d, completed=%p\n", ret, (void*)completed);
        fflush(stdout);

        if (ret < 0) {
            printf("Error: %s\n", strerror(errno));
            continue;
            if (errno == EAGAIN) {
                continue;  // No URB ready yet
            }
            perror("URB reap failed");
            break;
        }
        
        if (!completed) {
            continue;
        }
        
        // Process URB data
        int offset = 0;
        static int packet_count = 0;
        for (int i = 0; i < completed->number_of_packets; i++) {
            int actual = completed->iso_frame_desc[i].actual_length;
            
            if (actual > 2) {
                uint8_t *data = (uint8_t*)completed->buffer + offset;
                uint8_t header_len = data[0];
                uint8_t header_info = data[1];

                // Debug first few packets
                if (packet_count < 5) {
                    printf("\n=== Packet %d ===\n", packet_count);
                    printf("Actual length: %d\n", actual);
                    printf("UVC Header length: %d\n", header_len);
                    printf("UVC Header info: 0x%02X\n", header_info);
                    debug_print_hex("Raw packet data", data, actual);
                    
                    if (actual > header_len) {
                        debug_print_hex("Payload (after header)", 
                                       data + header_len, actual - header_len);
                    }
                    packet_count++;
                }

                if (header_len >= 2 && header_len <= 12 && actual > header_len) {
                    int payload_len = actual - header_len;
                    int ret = mjpeg_parser_add_data(&g_parser, data + header_len, 
                                         payload_len);
                    if (packet_count < 5) {
                        printf("Added %d bytes to parser, buffer now has %d bytes\n",
                        payload_len, g_parser.buffer_size);
                    }
                }
                else {
                    if (packet_count < 5) {
                        printf("Skipping packet: invalid header_len=%d or actual=%d\n",
                            header_len, actual);
                    }   
                }
            }
            offset += completed->iso_frame_desc[i].length;
        }
        
        // Try to extract frames
        printf("Try to extract frames...\n");
        if (g_parser.buffer_size > 10000) {
            printf("buffer_size > 10000\n");
            int frame_size;
            int result = mjpeg_parser_get_frame(&g_parser, g_jpeg_buffer, &frame_size);


           //while (mjpeg_parser_get_frame(&g_parser, g_jpeg_buffer, &frame_size) > 0) {
           //     // Decode JPEG with error handling
           //     if (frame_size > 100 && frame_size < MAX_JPEG_SIZE) {
           //         if (jpeg_decode_to_image(g_jpeg_buffer, frame_size, &g_current_frame) == 0) {
           //             // Process frame
           //             process_frame(&g_current_frame, frame_count, output);
           //             
           //             frame_count++;
           //             if (frame_count % 10 == 0) {
           //                 printf("\rFrames: %d/%d", frame_count, MAX_FRAMES);
           //                 fflush(stdout);
           //             }
           //             
           //             if (frame_count >= MAX_FRAMES) break;
           //         }
           //     }
           // }

            // new ver.
            if (result > 0) {
                printf("\n✓ Found frame! Size: %d bytes\n", frame_size);

                // Decode JPEG with error handling
                if (frame_size > 100 && frame_size < MAX_JPEG_SIZE) {
                    if (jpeg_decode_to_image(g_jpeg_buffer, frame_size, &g_current_frame) == 0) {
                        process_frame(&g_current_frame, frame_count, output);

                        frame_count++;
                        printf("\rFrames: %d/%d", frame_count, MAX_FRAMES);
                        fflush(stdout);

                        if (frame_count >= MAX_FRAMES) break;
                    } else {
                        printf("Failed to decode JPEG\n");
                    }
                }
            } 
            else if (result < 0) {
                printf("Parser error, cleared buffer\n");
            }
            
            // Resubmit URB
            if (frame_count < MAX_FRAMES) {
                if (ioctl(fd_usb, USBDEVFS_SUBMITURB, completed) < 0) {
                    perror("Failed to resubmit URB");
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
        int result = system(cmd);
        if (result == 0) {
            printf("MP4 created: output.mp4\n");
        }
        else {
            printf("FFmpeg conversion failed\n");
        }
    }

    fclose(output);
    
    // Cancel and free URBs
    for (int i = 0; i < NUM_URBS; i++) {
        if (urbs[i]) {
            ioctl(fd_usb, USBDEVFS_DISCARDURB, &urbs[i]->urb);
            // free(buffers[i]);
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
