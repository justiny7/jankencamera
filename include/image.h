#ifndef IMAGE_F_H
#define IMAGE_F_H

#include "camera.h"

#include <stdint.h>
#include <stdbool.h>

typedef uint32_t u32;
typedef int32_t  i32;

typedef enum {
    PIXEL_GRAY = 1,
    PIXEL_RGB = 3,
} PixelFormat;

typedef struct {
    uint16_t r, g, b;
} Pixel;

typedef struct {
    u32 width;
    u32 height;
    u32 size;
    u32 depth;
    PixelFormat fmt;

    bool is_bayer;
    BayerFormat bfmt;

    float* data;
    bool qpu_mem;
} Image;

Image* img_kmalloc();
Image* img_kmalloc_n();
u32 img_nbytes(const Image* img);
void img_kernel_init();

void img_init(Image* img, u32 width, u32 height, u32 depth, PixelFormat fmt);
void img_init_data(Image* img, u32 width, u32 height, u32 depth,
        PixelFormat fmt, float* data, bool qpu_mem);
void img_init_bayer(Image* img, u32 width, u32 height, u32 depth,
        uint8_t* buf, BayerFormat bfmt);
void img_init_frame(Image* img, CameraFrame* frame);
void img_free(Image* img);

void img_black_white_norm(Image* img, u32 white_lvl, u32 black_lvl);
void img_gray_world_wb(Image* img);
void img_debayer(Image* img);
void img_debayer_pipeline(Image* img, u32 white_lvl, u32 black_lvl);
void img_debayer_pipeline_to_fb(Image* img, u32* fb,
        u32 white_lvl, u32 black_lvl);
void img_debayer_pipeline_to_fb_frame(CameraFrame* frame, u32* fb,
        u32 white_lvl, u32 black_lvl);

void img_save_ppm(Image* img, const char* filename);
void img_write_framebuffer(Image* img, u32* fb);

float img_get_data(const Image* img, u32 y, u32 x, u32 c);
void img_like(Image* out, const Image* in);
void img_gray_like(Image* out, const Image* in);
void img_rgb_like(Image* out, const Image* in);
void img_to_grayscale(Image* out, const Image* in);

void img_set_data(Image* img, u32 y, u32 x, u32 c, float val);
void img_copy(Image* out, const Image* in);

void img_add(Image* out, const Image* a, const Image* b);
void img_sub(Image* out, const Image* a, const Image* b);
void img_mul(Image* out, const Image* a, const Image* b);

void img_mul_scalar_clamp(Image* img, float scalar, float mn, float mx);

#endif
