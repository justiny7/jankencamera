#include "image.h"
#include "kmem.h"
#include "math.h"
#include "lib.h"
#include "fat.h"
#include "mmu.h"
#include "unicam.h"

#include "kernel.h"
#include "fast_img_grayscale.h"
#include "fast_img_copy.h"
#include "fast_img_add.h"
#include "fast_img_sub.h"
#include "fast_img_mul_add.h"
#include "fast_img_mul_scalar_clamp.h"
#include "fast_fb_to_img.h"
#include "bw_norm_gw_accum.h"
#include "gw_wb.h"
#include "debayer_rggb.h"

#include <stddef.h>

#define SAFETY_CHECK 0

#if SAFETY_CHECK
#define assert(...) assert(__VA_ARGS__)
#else
#define assert(...)
#endif

#define LUMA_R 0.299f
#define LUMA_G 0.587f
#define LUMA_B 0.114f
static inline float luma(float r, float g, float b) {
    return r * LUMA_R + g * LUMA_G + b * LUMA_B;
}

// QPU stuff
#define NUM_QPUS 12
#define SIMD_WIDTH 16

static const u32 arena_size = 30 * 1024 * 1024;
static const u32 arena_align = SIMD_WIDTH * sizeof(float);
static Kernel fast_copy_k, fast_add_k, fast_sub_k,
              fast_mul_add_k, fast_mul_scalar_clamp_k,
              fast_grayscale_k, fast_fb_to_img_k;
static Kernel bw_norm_gw_accum_k, gw_wb_k, debayer_rggb_k;
static Arena data_arena;
static bool k_init;

// TODO:
// kernel for this and debayer
// try VPM for convolution kernels?
// reason why we don't have 2D abstraction for convolution: need 16-multiple width

void img_kernel_init() {
    if (k_init) return;

    arena_init_qpu(&data_arena, arena_size);

    kernel_init(&fast_grayscale_k, NUM_QPUS, 5,
            fast_img_grayscale, sizeof(fast_img_grayscale));
    kernel_init(&fast_copy_k, NUM_QPUS, 5,
            fast_img_copy, sizeof(fast_img_copy));
    kernel_init(&fast_add_k, NUM_QPUS, 6,
            fast_img_add, sizeof(fast_img_add));
    kernel_init(&fast_sub_k, NUM_QPUS, 6,
            fast_img_sub, sizeof(fast_img_sub));
    kernel_init(&fast_mul_add_k, NUM_QPUS, 6,
            fast_img_mul_add, sizeof(fast_img_mul_add));
    kernel_init(&fast_mul_scalar_clamp_k, NUM_QPUS, 7,
            fast_img_mul_scalar_clamp, sizeof(fast_img_mul_scalar_clamp));
    kernel_init(&fast_fb_to_img_k, NUM_QPUS, 7,
            fast_fb_to_img, sizeof(fast_fb_to_img));

    kernel_init(&bw_norm_gw_accum_k, NUM_QPUS, 12,
            bw_norm_gw_accum, sizeof(bw_norm_gw_accum));
    kernel_init(&gw_wb_k, NUM_QPUS, 8,
            gw_wb, sizeof(gw_wb));
    kernel_init(&debayer_rggb_k, NUM_QPUS, 7,
            debayer_rggb, sizeof(debayer_rggb));

    k_init = true;
}

Image* img_kmalloc() {
    return img_kmalloc_n(1);
}
Image* img_kmalloc_n(u32 n) {
    u32 sz = n * sizeof(Image);
    Image* res = (Image*) kvmalloc(sz);
    memset(res, 0, sz);
    return res;
}
u32 img_nbytes(const Image* img) {
    return img->size * img->fmt * sizeof(float);
}

// Guarantees:
// Image height is even
// Image width is divisble by 16
static bool check_img(Image* img) {
    return ((img->height & 1) == 0) &&
        ((img->width & 0xF) == 0);
}
void img_init(Image* img, u32 width, u32 height, u32 depth, PixelFormat fmt) {
    img->width = width;
    img->height = height;
    img->size = width * height;
    img->depth = depth;
    img->fmt = fmt;
    img->is_bayer = false;

    if (!img->data) {
        img->data = kvmalloc(img->size * img->fmt * sizeof(float));
        img->qpu_mem = false;
    } else {
        img->qpu_mem = true;
    }

#if SAFETY_CHECK
    assert(check_img(img), "img_init: didn't pass image check");
#endif
}
void img_init_data(Image* img, u32 width, u32 height, u32 depth, PixelFormat fmt,
        float* data, bool qpu_mem) {
    img->width = width;
    img->height = height;
    img->size = width * height;
    img->depth = depth;
    img->fmt = fmt;
    img->is_bayer = false;
    img->data = data;
    img->qpu_mem = qpu_mem;

#if SAFETY_CHECK
    assert(check_img(img), "img_init: didn't pass image check");
#endif
}
void img_init_bayer(Image* img, u32 width, u32 height, u32 depth,
        uint8_t* buf, BayerFormat bfmt) {
    assert(depth <= 16, "init bayer: depth must be at most 16 bits");

    img_init(img, width, height, depth, PIXEL_GRAY);
    img->is_bayer = true;
    img->bfmt = bfmt;

    if (depth <= 8) {
        for (u32 i = 0; i < img->size; i++) {
            img->data[i] = (float) buf[i];
        }
    } else {
        uint16_t* buf16 = (uint16_t*) buf;
        for (u32 i = 0; i < img->size; i++) {
            img->data[i] = (float) buf16[i];
        }
    }
}
void img_init_frame(Image* img, CameraFrame* frame) {
    CameraConfig cfg = frame->cfg;
    img_init_bayer(img, cfg.width, cfg.height, cfg.fmt,
            frame->buf->buf, cfg.bayer_fmt);
}
void img_free(Image* img) {
    if (!img->qpu_mem) kvfree(img->data);
    img->data = NULL;
    img->width = img->height = img->size = 0;
}

static inline u32 pixel_max(u32 depth) {
    return (1 << depth) - 1;
}
static u32 get_bayer_channel(Image* img, u32 i) {
    assert(img->is_bayer, "bayer channel: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "bayer channel: image must be gray");
    assert(i < img->size, "bayer channel: invalid idx");

    u32 y = (i / img->width) & 1;
    u32 x = (i % img->width) & 1;
    if (img->bfmt == BAYER_GRBG || img->bfmt == BAYER_BGGR) x ^= 1;
    if (img->bfmt == BAYER_GBRG || img->bfmt == BAYER_BGGR) y ^= 1;
    return y * 2 + x;
}
void img_black_white_norm(Image* img, u32 white_lvl, u32 black_lvl) {
    assert(img->is_bayer, "bw norm: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "bw norm: image must be gray");

    float diff = white_lvl - black_lvl;
    float mul = (float) pixel_max(img->depth) / diff;
    for (u32 i = 0; i < img->size; i++) {
        img->data[i] = clampf(img->data[i] - black_lvl, 0.f, diff) * mul;
    }
}

void img_gray_world_wb(Image* img) {
    assert(img->is_bayer, "white balance: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "white balance: image must be gray");

    float avg[4] = { 0.f, 0.f, 0.f, 0.f };
    for (u32 i = 0; i < img->size; i++) {
        u32 c = get_bayer_channel(img, i);
        avg[c] += img->data[i];
    }

    float mul = 4.f / img->size;
    for (u32 i = 0; i < 4; i++) avg[i] *= mul;

    float g_avg = (avg[1] + avg[2]) / 2;
    float pmax = (float) pixel_max(img->depth);
    for (u32 i = 0; i < img->size; i++) {
        u32 c = get_bayer_channel(img, i);
        float gain = g_avg / avg[c];
        img->data[i] = clampf(gain * img->data[i], 0.f, pmax);
    }
}

void img_bw_norm_gray_world_wb(Image* img, u32 white_lvl, u32 black_lvl) {
    assert(img->is_bayer, "bw norm & wb: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "bw norm & wb: image must be gray");
    assert(k_init, "img_bw_norm_gray_world_wb: kernels must be initialized");

    u32 arena_sz = data_arena.size;
    u32 w = img->width;
    u32 h = img->height;

    ////// black/white norm + accumulate sums for white balance
    float* accum[NUM_QPUS];
    u32* cnts[NUM_QPUS];
    float diff = white_lvl - black_lvl;
    float pmax = pixel_max(img->depth);
    float mul = pmax / diff;
    float gw_threshold = pmax * 0.90;

    kernel_reset_unifs(&bw_norm_gw_accum_k);
    for (u32 q = 0; q < NUM_QPUS; q++) {
        accum[q] = arena_alloc_align(&data_arena,
                SIMD_WIDTH * sizeof(float), arena_align);
        cnts[q] = arena_alloc_align(&data_arena,
                SIMD_WIDTH * sizeof(u32), arena_align);

        kernel_load_unif(&bw_norm_gw_accum_k, q, SIMD_WIDTH);
        kernel_load_unif(&bw_norm_gw_accum_k, q, w);
        kernel_load_unif(&bw_norm_gw_accum_k, q, NUM_QPUS);
        kernel_load_unif(&bw_norm_gw_accum_k, q, h);
        kernel_load_unif(&bw_norm_gw_accum_k, q, q);

        kernel_load_unif(&bw_norm_gw_accum_k, q, TO_BUS(img->data + q * w));
        kernel_load_unif(&bw_norm_gw_accum_k, q, TO_BUS(accum[q]));
        kernel_load_unif(&bw_norm_gw_accum_k, q, TO_BUS(cnts[q]));

        kernel_load_unif(&bw_norm_gw_accum_k, q, ((float) black_lvl));
        kernel_load_unif(&bw_norm_gw_accum_k, q, diff);
        kernel_load_unif(&bw_norm_gw_accum_k, q, mul);
        kernel_load_unif(&bw_norm_gw_accum_k, q, gw_threshold);
    }

    kernel_execute(&bw_norm_gw_accum_k);

    float avg[4] = { 0.f, 0.f, 0.f, 0.f };
    u32 cnt[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < NUM_QPUS; i++) {
        for (int j = 0; j < SIMD_WIDTH; j++) {
            u32 c = get_bayer_channel(img, i * w + j);
            avg[c] += accum[i][j];
            cnt[c] += cnts[i][j];
        }
    }

    for (int i = 0; i < 4; i++) {
        avg[i] /= max(cnt[i], 1.0);
    }

    float g_avg = (avg[1] + avg[2]) / 2;
    // float g_avg = (avg[0] + avg[1] + avg[2] + avg[3]) / 4.f;
    float* gains[2];
    for (int i = 0; i < 2; i++) {
        gains[i] = arena_alloc_align(&data_arena,
                SIMD_WIDTH * sizeof(float), arena_align);

        for (int j = 0; j < SIMD_WIDTH; j++) {
            gains[i][j] = g_avg / avg[get_bayer_channel(img, i * w + j)];
        }
    }

    /////// multiply gains
    kernel_reset_unifs(&gw_wb_k);
    for (u32 q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&gw_wb_k, q, SIMD_WIDTH);
        kernel_load_unif(&gw_wb_k, q, w);
        kernel_load_unif(&gw_wb_k, q, NUM_QPUS);
        kernel_load_unif(&gw_wb_k, q, h);
        kernel_load_unif(&gw_wb_k, q, q);

        kernel_load_unif(&gw_wb_k, q, TO_BUS(img->data + q * w));
        kernel_load_unif(&gw_wb_k, q, TO_BUS(gains[q & 1]));

        kernel_load_unif(&gw_wb_k, q, pmax);
    }

    kernel_execute(&gw_wb_k);

    arena_dealloc_to(&data_arena, arena_sz);
    mmu_flush_dcache();
}

static void debayer_linear_rgb(Image* img, u32 y, u32 x, float* r, float* g, float* b) {
    u32 s = img->width;
    u32 i = y * s + x;

    int xl = (x == 0 ? 1 : -1);
    int xr = (x == img->width - 1 ? -1 : 1);
    int yl = (y == 0 ? s : -s);
    int yr = (y == img->height - 1 ? -s : s);

    float* d = &img->data[i];

    u32 channel = get_bayer_channel(img, i);
    switch (channel) {
        case 0: // R
            *r = d[0];
            *g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
            *b = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
            break;
        case 1: // G1
            *r = (d[xl] + d[xr]) / 2.f;
            *g = d[0];
            *b = (d[yl] + d[yr]) / 2.f;
            break;
        case 2: // G2
            *r = (d[yl] + d[yr]) / 2.f;
            *g = d[0];
            *b = (d[xl] + d[xr]) / 2.f;
            break;
        case 3: // B
            *r = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
            *g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
            *b = d[0];
            break;
    }
}
void img_debayer(Image* img) {
    assert(img->is_bayer, "debayer: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "debayer: image must be gray");

    float* new_data = kvmalloc(img->size * 3 * sizeof(float));
    for (u32 y = 0; y < img->height; y++) {
        for (u32 x = 0; x < img->width; x++) {
            u32 i = y * img->width + x;
            debayer_linear_rgb(img, y, x,
                    &new_data[i * 3],
                    &new_data[i * 3 + 1],
                    &new_data[i * 3 + 2]);
        }
    }

    kvfree(img->data);
    img->data = new_data;
    img->fmt = PIXEL_RGB;
    img->is_bayer = false;
}
void img_debayer_fast(Image* img, u32* fb, bool store_img) {
    assert(img->is_bayer, "debayer_fast: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "debayer_fast: image must be gray");
    assert(img->bfmt == BAYER_RGGB, "debayer_fast: image must be RGGB");

    u32 h = img->height;
    u32 w = img->width;
    float pmax = (float) pixel_max(img->depth);

    u32 arena_sz = data_arena.size;
    if (!fb) {
        fb = arena_alloc_align(&data_arena, img->size * sizeof(u32), arena_align);
    }

    // normalize
    img_mul_scalar_clamp(img, 1.f / pmax, 0.f, 1.f);

    // tradeoff: compresses to 8 bits
    kernel_reset_unifs(&debayer_rggb_k);

    for (u32 q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&debayer_rggb_k, q, SIMD_WIDTH);
        kernel_load_unif(&debayer_rggb_k, q, w);
        kernel_load_unif(&debayer_rggb_k, q, NUM_QPUS * 2);
        kernel_load_unif(&debayer_rggb_k, q, h - 2); // h-2 rows
        kernel_load_unif(&debayer_rggb_k, q, q);

        // handles 2 rows at a time
        kernel_load_unif(&debayer_rggb_k, q, TO_BUS(img->data + (1 + q * 2) * w));
        kernel_load_unif(&debayer_rggb_k, q, TO_BUS(fb + (1 + q * 2) * w));
    }

    kernel_execute(&debayer_rggb_k);

    // borders
    const u32 ys[2] = { 0, h - 1};
    const u32 xs[2] = { 0, w - 1};

    for (u32 yi = 0; yi < 2; yi++) {
        u32 y = ys[yi];
        for (u32 x = 0; x < w; x++) {
            u32 i = y * w + x;
            float r, g, b;
            debayer_linear_rgb(img, y, x,
                    &r, &g, &b);

            u32 ri = (u32) (r * 255);
            u32 gi = (u32) (g * 255);
            u32 bi = (u32) (b * 255);

            fb[i] = (ri << 16) | (gi << 8) | bi;
        }
    }
    for (u32 y = 1; y < h - 1; y++) {
        for (u32 xi = 0; xi < 2; xi++) {
            u32 x = xs[xi];

            u32 i = y * w + x;
            float r, g, b;
            debayer_linear_rgb(img, y, x,
                    &r, &g, &b);

            u32 ri = (u32) (r * 255);
            u32 gi = (u32) (g * 255);
            u32 bi = (u32) (b * 255);

            fb[i] = (ri << 16) | (gi << 8) | bi;
        }
    }

    if (store_img) {
        float* new_data = kvmalloc(img->size * 3 * sizeof(float));

        // 8-bit to whatever depth
        float conv = pmax / 255.f;

        // 0: R = 16, G = 8,  B = 0,  R = 16, ...
        // 1: G = 8,  B = 0,  R = 16, G = 8,  ...
        // 2: B = 0,  R = 16, G = 8,  B = 0,  ...
        // Since QPU_NUM = 12 is divisible by 3, every QPU has the same shifts
        u32* shifts[3];
        for (int i = 0; i < 3; i++) {
            shifts[i] = arena_alloc_align(&data_arena,
                    SIMD_WIDTH * sizeof(u32), arena_align);

            for (int j = 0; j < SIMD_WIDTH; j++) {
                shifts[i][j] = 16 - ((j + i) % 3) * 8;
            }
        }

        kernel_reset_unifs(&fast_fb_to_img_k);
        for (int q = 0; q < NUM_QPUS; q++) {
            kernel_load_unif(&fast_fb_to_img_k, q, NUM_QPUS * SIMD_WIDTH);
            kernel_load_unif(&fast_fb_to_img_k, q, img->size * 3);
            kernel_load_unif(&fast_fb_to_img_k, q, q);

            kernel_load_unif(&fast_fb_to_img_k, q, TO_BUS(fb));
            kernel_load_unif(&fast_fb_to_img_k, q, TO_BUS(new_data + q * SIMD_WIDTH));
            kernel_load_unif(&fast_fb_to_img_k, q, TO_BUS(shifts[q % 3]));
            kernel_load_unif(&fast_fb_to_img_k, q, conv);
        }

        kernel_execute(&fast_fb_to_img_k);

        mmu_flush_dcache();

        kvfree(img->data);
        img->data = new_data;
        img->fmt = PIXEL_RGB;
        img->is_bayer = false;

    } else {
        // not gonna scale back bc if you're demosaicing, either you save the result
        // or you're just writing to framebuffer - just free image in that case
        img_free(img);
    }

    arena_dealloc_to(&data_arena, arena_sz);
    mmu_flush_dcache();
}

void img_debayer_pipeline_frame_to_fb(CameraFrame* frame, u32* fb,
        u32 white_lvl, u32 black_lvl) {
    CameraConfig cfg = frame->cfg;
    assert(cfg.fmt == CAM_FMT_BAYER_10, "only support 10-bit depth");

    Image img;
    img_init(&img, cfg.width, cfg.height, cfg.fmt, PIXEL_GRAY);

    float diff = white_lvl - black_lvl;
    float avg[4] = { 0.f, 0.f, 0.f, 0.f };

    uint16_t* buf = (uint16_t*) frame->buf->buf;
    float pmax = (float) pixel_max(img.depth);
    float mul = pmax / diff;
    for (u32 i = 0; i < img.size; i++) {
        u32 c = get_bayer_channel(&img, i);

        img.data[i] = clampf((float) buf[i] - black_lvl, 0.f, diff) * mul;
        avg[c] += img.data[i];
    }

    float avg_mul = 4.f / img.size;
    for (u32 i = 0; i < 4; i++) avg[i] *= avg_mul;

    float g_avg = (avg[1] + avg[2]) / 2;
    u32 shift = img.depth - 8;
    for (u32 y = 0; y < img.height; y++) {
        for (u32 x = 0; x < img.width; x++) {
            u32 s = img.width;
            u32 i = y * s + x;

            u32 channel = get_bayer_channel(&img, i);
            float gain = g_avg / avg[channel];
            img.data[i] = clampf(gain * img.data[i], 0.f, pmax);

            if (y > 1 && x > 1) {
                float r, g, b;
                debayer_linear_rgb(&img, y - 1, x - 1, &r, &g, &b);

                u32 ri = ((u32) r) >> shift;
                u32 gi = ((u32) g) >> shift;
                u32 bi = ((u32) b) >> shift;
                fb[i - 1 - s] = (ri << 16) | (gi << 8) | bi;
            }
        }
    }

    img_free(&img);
}

static u32 num_digits(u32 x) {
    if (x == 0) return 1;

    u32 res = 0;
    while (x > 0) {
        res++;
        x /= 10;
    }
    return res;
}
static void write_digits(u32 x, uint8_t** buf) {
    uint8_t* p = *buf;
    *buf = (p += num_digits(x));

    if (x == 0) {
        *(--p) = '0';
    }

    while (x > 0) {
        *(--p) = '0' + (x % 10);
        x /= 10;
    }
}
static u32 img_to_ppm(Image* img, uint8_t** buf) {
    const u32 targ_depth = 8;

    u32 pmax = pixel_max(targ_depth);
    u32 img_nbytes = img->size * img->fmt;
    u32 nbytes = 6 + // 'P', '6', '\n', ' ', '\n', '\n'
        num_digits(img->width) +
        num_digits(img->height) +
        num_digits(pmax) +
        img_nbytes;

    uint8_t* p = kvmalloc(nbytes);
    *buf = p;

    *p++ = 'P';
    *p++ = (img->fmt == PIXEL_RGB ? '6' : '5');
    *p++ = '\n';
    write_digits(img->width, &p);
    *p++ = ' ';
    write_digits(img->height, &p);
    *p++ = '\n';
    write_digits(pmax, &p);
    *p++ = '\n';

    if (img->depth != targ_depth) {
        printk("img_to_ppm: converting image from %d-bit to %d-bit\n",
                img->depth, targ_depth);

        if (img->depth > targ_depth) {
            u32 shift = img->depth - targ_depth;
            for (u32 i = 0; i < img_nbytes; i++) {
                p[i] = ((u32) img->data[i]) >> shift;
            }
        } else if (img->depth < targ_depth) {
            u32 shift = targ_depth - img->depth;
            for (u32 i = 0; i < img_nbytes; i++) {
                p[i] = ((u32) img->data[i]) << shift;
            }
        }
    }

    return nbytes;
}
void img_save_ppm(Image* img, const char* filename) {
    // TODO: arbitrary filenames?
    assert(filename[11] == '\0', "filename must be in 8.3 format");

    uint8_t* buf;
    u32 nbytes = img_to_ppm(img, &buf);

    fat_write_file(filename, buf, nbytes);
    kvfree(buf);
}
void img_write_framebuffer(Image* img, u32* fb) {
    assert(img->depth == 8 || img->depth == 10,
            "image write framebuffer: only supporting 8- or 10-bit depths");

    u32 shift = img->depth - 8;
    if (img->fmt == PIXEL_GRAY) {
        for (u32 i = 0; i < img->size; i++) {
            u32 col = ((u32) img->data[i]) >> shift;
            fb[i] = (col << 16) | (col << 8) | col;
        }
    } else {
        for (u32 i = 0; i < img->size; i++) {
            u32 r = ((u32) img->data[i * 3]) >> shift;
            u32 g = ((u32) img->data[i * 3 + 1]) >> shift;
            u32 b = ((u32) img->data[i * 3 + 2]) >> shift;
            fb[i] = (r << 16) | (g << 8) | b;
        }
    }
}

static inline bool in_bounds(const Image* img, u32 y, u32 x, u32 c) {
    return (y < img->height &&
            x < img->width &&
            c < img->fmt);
}
static inline u32 img_get_idx(const Image* img, u32 y, u32 x, u32 c) {
    assert(in_bounds(img, y, x, c), "get idx: out of bounds");
    return (y * img->width + x) * img->fmt + c;
}
float img_get_data(const Image* img, u32 y, u32 x, u32 c) {
    return img->data[img_get_idx(img, y, x, c)];
}
void img_set_data(Image* img, u32 y, u32 x, u32 c, float val) {
    img->data[img_get_idx(img, y, x, c)] = val;
}
void img_copy(Image* out, const Image* in) {
    assert(k_init, "img_copy: kernels must be initialized");

    img_like(out, in);

    kernel_reset_unifs(&fast_copy_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_copy_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_copy_k, q, out->size * out->fmt);
        kernel_load_unif(&fast_copy_k, q, q);

        kernel_load_unif(&fast_copy_k, q, TO_BUS(in->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_copy_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&fast_copy_k);

    mmu_flush_dcache();
}

void img_like(Image* out, const Image* in) {
    img_init(out, in->width, in->height, in->depth, in->fmt);
}
void img_gray_like(Image* out, const Image* in) {
    img_init(out, in->width, in->height, in->depth, PIXEL_GRAY);
}
void img_rgb_like(Image* out, const Image* in) {
    img_init(out, in->width, in->height, in->depth, PIXEL_RGB);
}
void img_to_grayscale(Image* out, const Image* in) {
    assert(in->fmt == PIXEL_RGB, "to grayscale: needs RGB");
    assert(k_init, "img_to_grayscale: kernels must be initialized");

    img_gray_like(out, in);

    kernel_reset_unifs(&fast_grayscale_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_grayscale_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_grayscale_k, q, out->size * out->fmt);
        kernel_load_unif(&fast_grayscale_k, q, q);

        kernel_load_unif(&fast_grayscale_k, q, TO_BUS(in->data));
        kernel_load_unif(&fast_grayscale_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&fast_grayscale_k);

    mmu_flush_dcache();
}

static inline bool same_shape(const Image* a, const Image* b) {
    return (a->width == b->width &&
            a->height == b->height &&
            a->fmt == b->fmt);
}
void img_add(Image* out, const Image* a, const Image* b) {
    assert(same_shape(a, b), "img_add: needs same shape");
    assert(k_init, "img_add: kernels must be initialized");

    img_like(out, a);

    kernel_reset_unifs(&fast_add_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_add_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_add_k, q, out->size * out->fmt);
        kernel_load_unif(&fast_add_k, q, q);

        kernel_load_unif(&fast_add_k, q, TO_BUS(a->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_add_k, q, TO_BUS(b->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_add_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&fast_add_k);

    mmu_flush_dcache();
}
void img_sub(Image* out, const Image* a, const Image* b) {
    assert(same_shape(a, b), "img_sub: needs same shape");
    assert(k_init, "img_sub: kernels must be initialized");

    img_like(out, a);

    kernel_reset_unifs(&fast_sub_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_sub_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_sub_k, q, out->size * out->fmt);
        kernel_load_unif(&fast_sub_k, q, q);

        kernel_load_unif(&fast_sub_k, q, TO_BUS(a->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_sub_k, q, TO_BUS(b->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_sub_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&fast_sub_k);

    mmu_flush_dcache();
}
void img_mul(Image* out, const Image* a, const Image* b) {
    assert(a->width == b->width && a->height == b->height,
            "img_mul: needs same width/height");
    assert(k_init, "img_mul: kernels must be initialized");

    img_like(out, a);

    u32 d = (b->fmt == PIXEL_RGB ? 1 : 3); // alow for broadcasting
    for (u32 i = 0; i < out->size * out->fmt; i++) {
        out->data[i] = a->data[i] * b->data[i / d];
    }

    mmu_flush_dcache();
}
void img_mul_add(Image* out, const Image* a, const Image* b) {
    assert(a->width == b->width && a->height == b->height,
            "img_mul_add: needs same width/height");
    assert(a->fmt == PIXEL_RGB && b->fmt == PIXEL_GRAY,
            "img_mul_add: needs a to be RGB and b to be GRAY");
    assert(out->data, "img_mul_add: needs out data allocated");
    assert(k_init, "img_mul_add: kernels must be initialized");

    kernel_reset_unifs(&fast_mul_add_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_mul_add_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_mul_add_k, q, out->size * out->fmt);
        kernel_load_unif(&fast_mul_add_k, q, q);

        kernel_load_unif(&fast_mul_add_k, q, TO_BUS(a->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_mul_add_k, q, TO_BUS(b->data));
        kernel_load_unif(&fast_mul_add_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&fast_mul_add_k);

    mmu_flush_dcache();
}
void img_mul_scalar_clamp(Image* img, float scalar, float mn, float mx) {
    assert(img->data, "img_mul_scalar_clamp: needs data allocated");
    assert(k_init, "img_mul_scalar_clamp: kernels must be initialized");

    kernel_reset_unifs(&fast_mul_scalar_clamp_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, img->size * img->fmt);
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, q);

        kernel_load_unif(&fast_mul_scalar_clamp_k, q, TO_BUS(img->data + q * SIMD_WIDTH));
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, scalar);
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, mn);
        kernel_load_unif(&fast_mul_scalar_clamp_k, q, mx);
    }

    kernel_execute(&fast_mul_scalar_clamp_k);

    mmu_flush_dcache();
}
