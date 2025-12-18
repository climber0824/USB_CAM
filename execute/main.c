#include "uvc_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define NUM_URBS 4

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s /dev/bus/usb/BBB/DDD\n", argv[0]);
        printf("Find your camera with: lsusb\n");
        return 1;
    }
    
    // Open USB device
    int fd_usb = open(argv[1], O_RDWR);
    if (fd_usb < 0) {
        perror("Failed to open USB device");
        printf("Try: sudo %s %s\n", argv[0], argv[1]);
        return 1;
    }
    
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
    
    // Set alternate setting to enable streaming
    printf("\nEnabling video streaming...\n");
    if (set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, 1) < 0) {
        goto cleanup;
    }
    
    // Open output file for video data
    FILE *output = fopen("camera_output.raw", "wb");
    if (!output) {
        perror("Failed to open output file");
        goto cleanup;
    }
    
    printf("\nCapturing video stream (Ctrl+C to stop)...\n");
    printf("Output: camera_output.raw\n");
    printf("Progress: ");
    fflush(stdout);
    
    // Submit multiple URBs for continuous streaming
    struct uvc_urb urbs[NUM_URBS];
    int endpoint = 0x81;
    
    // Submit initial URBs
    for (int i = 0; i < NUM_URBS; i++) {
        if (submit_iso_urb(fd_usb, &urbs[i], endpoint, 32, 3072) < 0) {
            printf("Failed to submit initial URB %d\n", i);
        }
    }
    
    // Capture loop - capture 300 frames then stop
    int frame_count = 0;
    int max_frames = 300;
    
    while (frame_count < max_frames) {
        struct usbdevfs_urb *completed_urb = reap_urb(fd_usb, 1000);
        
        if (completed_urb) {
            // Process the received data
            int offset = 0;
            for (int i = 0; i < completed_urb->number_of_packets; i++) {
                int actual_length = completed_urb->iso_frame_desc[i].actual_length;
                if (actual_length > 0) {
                    process_video_data(
                        (unsigned char*)completed_urb->buffer + offset,
                        actual_length,
                        output
                    );
                    
                    // Count frames
                    if (((unsigned char*)completed_urb->buffer + offset)[1] & 0x02) {
                        frame_count++;
                        if (frame_count >= max_frames) break;
                    }
                }
                offset += completed_urb->iso_frame_desc[i].length;
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
    fclose(output);
    
    // Cancel any pending URBs
    for (int i = 0; i < NUM_URBS; i++) {
        ioctl(fd_usb, USBDEVFS_DISCARDURB, &urbs[i].urb);
    }
    
    // Disable streaming
    set_interface_alt_setting(fd_usb, USB_VIDEO_STREAMING_INTERFACE, 0);
    
cleanup:
    release_interface(fd_usb, USB_VIDEO_STREAMING_INTERFACE);
    release_interface(fd_usb, USB_VIDEO_CONTROL_INTERFACE);
    close(fd_usb);
    
    printf("Done!\n");
    printf("\nNote: Output is raw video data (likely MJPEG or YUV).\n");
    printf("You'll need to parse the format based on your camera specs.\n");
    
    return 0;
}
