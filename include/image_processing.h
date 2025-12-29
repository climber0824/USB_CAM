#ifndef IMAGE_PROCESSING_H
#define IMAGE_PROCESSING_H

#include <stdint.h>
#include "config.h"

// Fixed size image structure
typedef struct {
    uint8_t data[MAX_FRAME_SIZE];
    int width;
    int height;
    int channels;
    int step;
    int valid;      // to indicate if image is valid
} Image;

// Initialize image structures
void image_init(Image *img, int width, int height, int channels);
void image_clear(Image *img);
void image_copy(const Image *src, Image *dst);

// Basic operations
void image_to_grayscale(Image *img);
void image_adjust_brightness(Image *img, int delta);
void image_adjust_contrast(Image *img, float factor);

// Drawing functions
void image_draw_rect(Image *img, int x, int y, int w, int h,
                    uint8_t r, uint8_t g, uint8_t b, int thickness);
void image_draw_line(Image *img, int x1, int y1, int x2, int y2,
                    uint8_t r, uint8_t g, uint8_t b);

// Utility functions
void image_set_pixel(Image *img, int x, int y, uint8_t r, uint8_t g, uint8_t b);
void image_get_pixel(const Image *img, int x, int y, uint8_t *r, uint8_t *g, uint8_t *b);

#endif // IMAGE_PROCESSING_H
