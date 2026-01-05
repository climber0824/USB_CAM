#ifndef URB_MANAGER_H
#define URB_MANAGER_H

#include <linux/usbdevice_fs.h>
#include <stdint.h>
#include "config.h"

typedef struct {
    struct usbdevfs_urb urb;
    struct usbdevfs_iso_packet_desc iso_packets[MAX_ISO_PACKETS];
    uint8_t buffer[URB_BUFFER_SIZE];
    int active;
} URBBuffer;

typedef struct {
    URBBuffer urbs[NUM_URBS];
    int num_active;
} URBManager;

void urb_manager_init(URBManager *mgr);
int urb_submit(int fd, URBBuffer *buffer, int end_point, int num_packets, int packet_size);
struct usbdevfs_urb *urb_reap(int fd);

#endif  // URB_MANAGER_H
