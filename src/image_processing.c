#include <string.h>
#include "image_processing.h"

void image_init(Image *img, int width, int height, int channels) {
    if (width > MAX_FRAME_WIDTH || height > MAX_FRAME_HEIGHT) {
        img->valid = 0;
        printf("img init error");
        return;
    }

    img->width = width;
    img->height = height;
    img->channels = channels;
    img->step = width * channels;
    img->valid = 1;
    memset(img->data, 0, sizeof(img->data));
}

void image_clear(Image *img) {
    memset(img->data, 0, sizeof(img->data));
    img->valid = 0;
}

void image_copy(const Image *src, Image *dst) {
    if (!src->valid) {
        printf("img copy error");
        return;
    }

    dst->width = src->width;
    dst->height = src->height;
    dst->channels = src->channels;
    dst->step = src->step;
    dst->valid = src->valid;

    int size = src->height * src->step;
    memcpy(dst->data, src->data, size);
}

void image_to_grayscale(Image *img) {
    if (!img->valid || img->channels != 3) {
        printf("img grayscale error");
        return;
    }
    
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            int idx = y * img->step + x * 3;
            uint8_t r = img->data[idx];
            uint8_t g = img->data[idx + 1];
            uint8_t b = img->data[idx + 2];

            uint8_t gray = (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
            img->data[idx] = gray;
            img->data[idx + 1] = gray;
            img->data[idx + 2] = gray;
        }
    }
}

void image_adjust_contrast(Image *img, float factor) {
    if (!img->valid) {
        printf("img is not valid at contrast");
        return;
    }

    int size = img->height * img->step;
    for (int i = 0; i < size; i++) {
        int val = (int)((img->data[i] - 128) * factor + 128);
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        img->data[i] = (uint8_t)val;
    }
}

void image_set_pixel(Image *img, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (!img->valid || x < 0 || x >= img->width || y < 0 || y >= img->height) return;
    if (img->channels != 3) {
        print("Invalid channels when setting pixel");
        return;
    }
    
    int idx = y * img->step + x * 3;
    img->data[idx] = r;
    img->data[idx + 1] = g;
    img->data[idx + 2] = b;
}

void image_draw_rect(Image *img, int x, int y, int w, int h,
                     uint8_t r, uint8_t g, uint8_t b, int thickness) {
    if (!img->valid) return;
    
    for (int t = 0; t < thickness; t++) {
        // Top and bottom
        for (int i = x; i < x + w; i++) {
            image_set_pixel(img, i, y + t, r, g, b);
            image_set_pixel(img, i, y + h - t - 1, r, g, b);
        }
        
        // Left and right
        for (int i = y; i < y + h; i++) {
            image_set_pixel(img, x + t, i, r, g, b);
            image_set_pixel(img, x + w - t - 1, i, r, g, b);
        }
    }
}

void image_draw_line(Image *img, int x1, int y1, int x2, int y2,
                     uint8_t r, uint8_t g, uint8_t b) {
    if (!img->valid) return;

    // Bresenham's line algorithm
    int dx = x2 - x1;
    int dy = y2 - y1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        image_set_pixel(img, x1, y1, r, g, b);

        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}
