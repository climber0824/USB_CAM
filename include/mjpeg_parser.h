#ifndef MJPEG_PARSER_H
#define MJPEG_PARSER_H

#include <stdint.h>
#include "config.h"

typedef struct {
    uint8_t buffer[MJPEG_BUFFER_SIZE];
    uint8_t jpeg_frame[MAX_JPEG_SIZE];
    int buffer_head;
    int buffer_size;
    int frame_count;
} MJPEGParser;

void mjpeg_parser_init(MJPEGParser *parser);
int mjpeg_parser_add_data(MJPEGParser *parser, const uint8_t *data, int length);
int mjpeg_parser_get_frame(MJPEGParser *parser, uint8_t *frame_out, int *frame_size);

#endif // MJPEG_PARSER_H
