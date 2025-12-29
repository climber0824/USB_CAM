#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>
#include "urb_manager.h"

void urb_manager_init(URBManager *mgr) {
    memset(mgr, 0, sizeof(URBManager));
    mgr->num_active = 0;

    for (int i = 0; i < NUM_URBS; i++) {
        mgr->urbs[i].active = 0;
    }
}

int urb_submit(int fd, URBBuffer *urb_buf, int endpoint, int num_packets, int packet_size) {
    memset(&urb_buf->urb, 0, sizeof(struct urbdevfs_urb));

    urb_buf->urb.type = USBDEVFS_URB_TYPE_ISO;
    urb_buf->urb.endpoint = endpoint;
    urb_buf->urb.buffer = urb_buf->buffer;
    urb_buf->urb.buffer_length = num_packets * packet_size;
    urb_buf->urb.number_of_packets = num_packets;

    for (int i = 0; i < num_packets; i++) {
        urb_buf->iso_packets[i].length = packet_size;
        urb_buf->iso_packets[i].actual_length = 0;
        urb_buf->iso_packets[i].status = 0;
    }

    if (ioctl(fd, USBDEVFS_SUBMITURB, &urb_buf->urb) < 0) {
        return -1;
    }

    urb_buf->active = 1;
    
    return 0;
}

struct usbdevfs_urb* urb_reap(int fd) {
    struct usbdevfs_urb *urb;

    if (ioctl(fd, USBDEVFS_REAPURB, &urb) < 0) {
        if (errno == EAGAIN) {
            printf("Resource temporarily unavailable\n");
        }
        else(errno == ENODEV) {
            printf("Operation not supported by device\n");
        }
        else {
            printf("Other errors\n");
        }
        return NULL;
    }

    return urb;
}
