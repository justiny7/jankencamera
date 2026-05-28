#include "image.h"
#include "kmem.h"
#include "math.h"
#include "lib.h"
#include "fat.h"

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

void img_init(Image* img, u32 width, u32 height, u32 depth, PixelFormat fmt) {
    img->width = width;
    img->height = height;
    img->size = width * height;
    img->depth = depth;
    img->fmt = fmt;
    img->is_bayer = false;

    u32 sz = img->size * img->fmt * sizeof(float);
    img->data = kmalloc(sz);
}
void img_init_data(Image* img, u32 width, u32 height, u32 depth, PixelFormat fmt,
        float* data) {
    img->width = width;
    img->height = height;
    img->size = width * height;
    img->depth = depth;
    img->fmt = fmt;
    img->is_bayer = false;
    img->data = data;
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
    kfree(img->data);
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

    for (u32 i = 0; i < 4; i++) avg[i] /= 4.f;

    float g_avg = (avg[1] + avg[2]) / 2;
    float pmax = (float) pixel_max(img->depth);
    for (u32 i = 0; i < img->size; i++) {
        u32 c = get_bayer_channel(img, i);
        float gain = g_avg / avg[c];
        img->data[i] = clampf(gain * img->data[i], 0.f, pmax);
    }
}
void img_debayer(Image* img) {
    assert(img->is_bayer, "debayer: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "debayer: image must be gray");

    float* new_data = kmalloc(img->size * 3 * sizeof(float));
    for (u32 y = 0; y < img->height; y++) {
        for (u32 x = 0; x < img->width; x++) {
            u32 s = img->width;
            u32 i = y * s + x;

            int xl = (x == 0 ? 1 : -1);
            int xr = (x == img->width - 1 ? -1 : 1);
            int yl = (y == 0 ? s : -s);
            int yr = (y == img->height - 1 ? -s : s);

            float r = 0, g = 0, b = 0;
            float* d = &img->data[i];

            u32 channel = get_bayer_channel(img, i);
            switch (channel) {
                case 0: // R
                    r = d[0];
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    break;
                case 1: // G1
                    r = (d[xl] + d[xr]) / 2.f;
                    g = d[0];
                    b = (d[yl] + d[yr]) / 2.f;
                    break;
                case 2: // G2
                    r = (d[yl] + d[yr]) / 2.f;
                    g = d[0];
                    b = (d[xl] + d[xr]) / 2.f;
                    break;
                case 3: // B
                    r = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = d[0];
                    break;
            }

            new_data[i * 3] = r;
            new_data[i * 3 + 1] = g;
            new_data[i * 3 + 2] = b;
        }
    }

    kfree(img->data);
    img->data = new_data;
    img->fmt = PIXEL_RGB;
    img->is_bayer = false;
}
void img_debayer_pipeline(Image* img, u32 white_lvl, u32 black_lvl) {
    assert(img->is_bayer, "debayer pipeline: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "debayer pipeline: image must be gray");

    float diff = white_lvl - black_lvl;
    float avg[4] = { 0.f, 0.f, 0.f, 0.f };

    float pmax = (float) pixel_max(img->depth);
    float mul = pmax / diff;
    for (u32 i = 0; i < img->size; i++) {
        u32 c = get_bayer_channel(img, i);

        img->data[i] = clampf(img->data[i] - black_lvl, 0.f, diff) * mul;
        avg[c] += img->data[i];
    }

    for (u32 i = 0; i < 4; i++) avg[i] /= 4.f;

    float g_avg = (avg[1] + avg[2]) / 2;
    float* new_data = kmalloc(img->size * 3 * sizeof(float));
    for (u32 y = 0; y < img->height; y++) {
        for (u32 x = 0; x < img->width; x++) {
            u32 s = img->width;
            u32 i = y * s + x;

            u32 channel = get_bayer_channel(img, i);
            float gain = g_avg / avg[channel];
            img->data[i] = clampf(gain * img->data[i], 0.f, pmax);

            int xl = (x == 0 ? 1 : -1);
            int xr = (x == img->width - 1 ? -1 : 1);
            int yl = (y == 0 ? s : -s);
            int yr = (y == img->height - 1 ? -s : s);

            float r = 0, g = 0, b = 0;
            float* d = &img->data[i];

            switch (channel) {
                case 0: // R
                    r = d[0];
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    break;
                case 1: // G1
                    r = (d[xl] + d[xr]) / 2.f;
                    g = d[0];
                    b = (d[yl] + d[yr]) / 2.f;
                    break;
                case 2: // G2
                    r = (d[yl] + d[yr]) / 2.f;
                    g = d[0];
                    b = (d[xl] + d[xr]) / 2.f;
                    break;
                case 3: // B
                    r = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = d[0];
                    break;
            }

            new_data[i * 3] = r;
            new_data[i * 3 + 1] = g;
            new_data[i * 3 + 2] = b;
        }
    }

    kfree(img->data);
    img->data = new_data;
    img->fmt = PIXEL_RGB;
    img->is_bayer = false;
}
void img_debayer_pipeline_to_fb(Image* img, u32* fb,
        u32 white_lvl, u32 black_lvl) {
    assert(img->is_bayer, "debayer pipeline to fb: image must be bayer");
    assert(img->fmt == PIXEL_GRAY, "debayer pipeline to fb: image must be gray");
    assert(img->depth == 8 || img->depth == 10,
            "debayer pipeline to fb: only support 8/10 bit depth");

    float diff = white_lvl - black_lvl;
    float avg[4] = { 0.f, 0.f, 0.f, 0.f };

    float pmax = (float) pixel_max(img->depth);
    float mul = pmax / diff;
    for (u32 i = 0; i < img->size; i++) {
        u32 c = get_bayer_channel(img, i);

        img->data[i] = clampf(img->data[i] - black_lvl, 0.f, diff) * mul;
        avg[c] += img->data[i];
    }

    for (u32 i = 0; i < 4; i++) avg[i] /= 4.f;

    float g_avg = (avg[1] + avg[2]) / 2;
    u32 shift = img->depth - 8;
    for (u32 y = 0; y < img->height; y++) {
        for (u32 x = 0; x < img->width; x++) {
            u32 s = img->width;
            u32 i = y * s + x;

            u32 channel = get_bayer_channel(img, i);
            float gain = g_avg / avg[channel];
            img->data[i] = clampf(gain * img->data[i], 0.f, pmax);

            int xl = (x == 0 ? 1 : -1);
            int xr = (x == img->width - 1 ? -1 : 1);
            int yl = (y == 0 ? s : -s);
            int yr = (y == img->height - 1 ? -s : s);

            float r = 0, g = 0, b = 0;
            float* d = &img->data[i];

            switch (channel) {
                case 0: // R
                    r = d[0];
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    break;
                case 1: // G1
                    r = (d[xl] + d[xr]) / 2.f;
                    g = d[0];
                    b = (d[yl] + d[yr]) / 2.f;
                    break;
                case 2: // G2
                    r = (d[yl] + d[yr]) / 2.f;
                    g = d[0];
                    b = (d[xl] + d[xr]) / 2.f;
                    break;
                case 3: // B
                    r = (d[xl + yl] + d[xl + yr] + d[xr + yl] + d[xr + yr]) / 4.f;
                    g = (d[xl] + d[xr] + d[yl] + d[yr]) / 4.f;
                    b = d[0];
                    break;
            }

            u32 ri = ((u32) r) >> shift;
            u32 gi = ((u32) g) >> shift;
            u32 bi = ((u32) b) >> shift;
            fb[i] = (ri << 16) | (gi << 8) | bi;
        }
    }
}
void img_debayer_pipeline_to_fb_frame(CameraFrame* frame, u32* fb,
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

    for (u32 i = 0; i < 4; i++) avg[i] /= 4.f;

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
                u32 j = i - 1 - s;
                float r = 0, g = 0, b = 0;
                float* d = &img.data[j];

                switch (channel ^ 0x3) { // since we're doing up diagonal
                    case 0: // R
                        r = d[0];
                        g = (d[-1] + d[1] + d[-s] + d[s]) / 4.f;
                        b = (d[-1 + -s] + d[-1 + s] + d[1 + -s] + d[1 + s]) / 4.f;
                        break;
                    case 1: // G1
                        r = (d[-1] + d[1]) / 2.f;
                        g = d[0];
                        b = (d[-s] + d[s]) / 2.f;
                        break;
                    case 2: // G2
                        r = (d[-s] + d[s]) / 2.f;
                        g = d[0];
                        b = (d[-1] + d[1]) / 2.f;
                        break;
                    case 3: // B
                        r = (d[-1 + -s] + d[-1 + s] + d[1 + -s] + d[1 + s]) / 4.f;
                        g = (d[-1] + d[1] + d[-s] + d[s]) / 4.f;
                        b = d[0];
                        break;
                }

                u32 ri = ((u32) r) >> shift;
                u32 gi = ((u32) g) >> shift;
                u32 bi = ((u32) b) >> shift;
                fb[j] = (ri << 16) | (gi << 8) | bi;
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

    uint8_t* p = kmalloc(nbytes);
    *buf = p;

    *p++ = 'P';
    *p++ = '6';
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
    kfree(buf);
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
    img_like(out, in);
    for (u32 i = 0; i < in->size * in->fmt; i++) {
        out->data[i] = in->data[i];
    }
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

    img_gray_like(out, in);
    for (u32 i = 0; i < out->size; i++) {
        out->data[i] = luma(in->data[i * 3], in->data[i * 3 + 1], in->data[i * 3 + 2]);
    }
}

static inline bool same_shape(const Image* a, const Image* b) {
    return (a->width == b->width &&
            a->height == b->height &&
            a->fmt == b->fmt);
}
void img_add(Image* out, const Image* a, const Image* b) {
    assert(same_shape(a, b), "img_add: needs same shape");

    img_like(out, a);
    for (u32 i = 0; i < out->size * out->fmt; i++) {
        out->data[i] = a->data[i] + b->data[i];
    }
}
void img_sub(Image* out, const Image* a, const Image* b) {
    assert(same_shape(a, b), "img_sub: needs same shape");

    img_like(out, a);
    for (u32 i = 0; i < out->size * out->fmt; i++) {
        out->data[i] = a->data[i] - b->data[i];
    }
}
void img_mul(Image* out, const Image* a, const Image* b) {
    assert(a->width == b->width && a->height == b->height,
            "img_mul: needs same width/height");

    img_like(out, a);

    u32 d = (b->fmt == PIXEL_RGB ? 1 : 3); // alow for broadcasting
    for (u32 i = 0; i < out->size * out->fmt; i++) {
        out->data[i] = a->data[i] * b->data[i / d];
    }
}
