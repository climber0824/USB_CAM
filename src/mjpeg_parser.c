#include <string.h>
#include "mjpeg_parser.h"

void mjpeg_parser_init(MJPEGParser *parser) {
    memset(parser, 0, sizeof(MJPEGParser));
    parser->buffer_head = 0;
    parser->buffer_size = 0;
    parser->frame_count = 0;
}

int mjpeg_parser_add_data(MJPEGParser *parser, const uint8_t *data, int length) {
    if (!parser || !data) {
        printf("mjpeg_parser_add_data error\n");
        return -1;
    }

    // Check if buffer has space
    if (parser->buffer_size + length > MJPEG_BUFFER_SIZE) {
        // Buffer full, discard oldest data
        int discard = (parser->buffer_head + length) - MJPEG_BUFFER_SIZE;
        if (discard < parser->buffer_size) {
            memmove(parser->buffer, parser->buffer + discard, parser->buffer_size - discard);
            parser->buffer_size -= discard;
        }
        else {
            parser->buffer_size = 0;
        }
    }

    // Add new data
    memcpy(parser->buffer + parser->buffer_size, data, length);
    parser->buffer_size += length;

    return 0;
}

int mjpeg_parser_get_frame(MJPEGParser *parser, uint8_t *frame_out, int *frame_size) {
    if (!parser) {
        printf("parser get frame error\n");
        return -1;
    }
    
    // Find JPEG markers: SOI(0xFFD8) and EOI(0xFFD9)
    int soi = -1, eoi = -1;
    
    for (int i = 0; i < parser->buffer_size - 1; i++) {
        if (parser->buffer[i] == 0xFF && parser->buffer[i + 1] == 0xD8) {
            soi = i;
            break;
        }
    }

    if (soi < 0) {
        printf("parger get frame: can't find soi");
        return -1;
    }

    for (int i = soi + 2; i < parser->buffer_size - 1; i++) {
        if (parser->buffer[i] == 0xFF && parser->buffer[i + 1] == 0xD9) {
            eoi = i + 2;
            break;
        }
    }

    if (soi >= 0 && eoi > soi) {
        // Find complete JPEG frame
        *frame_size = eoi - soi;

        if (*frame_size > MAX_JPEG_SIZE) {
            // Too large, skip it
            printf("Frame is too large\n");
            parser->buffer_size = 0;
            return -1;
        }

        // Copy frame
        memcpy(frame_out, parser->buffer + soi, *frame_size);
        parser->frame_count++;

        // Remove processed data
        int remaining = parser->buffer_size - eoi;
        if (remaining > 0) {
            memmove(parser->buffer, parser->buffer + eoi, remaining);
        }

        parser->buffer_size = remaining;

        return 1;   // Frame fount
    }

    return 0;   // No complete frame yet
}
