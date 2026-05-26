#include "mertens.h"
#include "math.h"
#include "kmem.h"
#include "lib.h"
#include "sys_timer.h"

#include "kernel.h"
#include "gaussian_conv.h"

#define VERBOSE 1

static const float LN2 = 0.693147f;
static const float F_EPS = 1e-6f;
static const u32 gaussian_kernel_size = 5;
static const float gaussian_kernel[5] = {
    0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f
};

// QPU stuff
#define NUM_QPUS 12
#define SIMD_WIDTH 16

static const u32 arena_size = 80 * 1024 * 1024;
static const u32 img_max_size = 1920 * 1080 * sizeof(float);
static Arena arena;
static Kernel gconv_k;
static float* gconv_data[3];
static bool k_init;

static void convolve_laplacian_abs(Image* out, const Image* in) {
    u32 w = in->width;
    u32 h = in->height;
    u32 channels = in->fmt;

    img_like(out, in);
    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            for (u32 c = 0; c < channels; c++) {
                u32 xl = (x == 0 ? 1 : x - 1);
                u32 xr = (x == w - 1 ? w - 2 : x + 1);
                u32 yl = (y == 0 ? 1 : y - 1);
                u32 yr = (y == h - 1 ? h - 2 : y + 1);
                float sum = img_get_data(in, y, xl, c) +
                    img_get_data(in, y, xr, c) +
                    img_get_data(in, yl, x, c) +
                    img_get_data(in, yr, x, c) -
                    img_get_data(in, y, x, c) * 4.f;

                if (sum < 0) sum = -sum;
                img_set_data(out, y, x, c, sum);
            }
        }
    }
}
static void convolve_gaussian(Image* out, const Image* in, float mul) {
    i32 r = gaussian_kernel_size / 2;

    u32 w = in->width;
    u32 h = in->height;
    u32 channels = in->fmt;

    u32 offset = w * channels * 2;

    memcpy(gconv_data[0], in->data, in->size * channels * sizeof(float));

    ////////// horizontal blur
    kernel_reset_unifs(&gconv_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&gconv_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&gconv_k, q, w * (h - 4) * channels);
        kernel_load_unif(&gconv_k, q, q);

        kernel_load_unif(&gconv_k, q, 1 * channels);
        kernel_load_unif(&gconv_k, q, mul);

        kernel_load_unif(&gconv_k, q, TO_BUS(gconv_data[0] + offset + q * SIMD_WIDTH));
        kernel_load_unif(&gconv_k, q, TO_BUS(gconv_data[1] + offset + q * SIMD_WIDTH));
    }

    kernel_execute(&gconv_k);

    const float ys[4] = { 0, 1, h - 2, h - 1 };
    const float xs[4] = { 0, 1, w - 2, w - 1 };

    for (u32 yi = 0; yi < 4; yi++) {
        u32 y = ys[yi];
        for (u32 x = 0; x < w; x++) {
            for (u32 c = 0; c < channels; c++) {
                float sum = 0.f;
                
                for (i32 k = -r; k <= r; k++) {
                    i32 px = x + k;
                    if (px < 0) px = -px;
                    if (px >= (i32) w) px = 2 * (w - 1) - px;
                    sum += gconv_data[0][(y * w + px) * channels + c] * gaussian_kernel[k + r];
                }

                gconv_data[1][(y * w + x) * channels + c] = sum * mul;
            }
        }
    }
    for (u32 y = 2; y < h - 2; y++) {
        for (u32 xi = 0; xi < 4; xi++) {
            u32 x = xs[xi];
            for (u32 c = 0; c < channels; c++) {
                float sum = 0.f;
                
                for (i32 k = -r; k <= r; k++) {
                    i32 px = x + k;
                    if (px < 0) px = -px;
                    if (px >= (i32) w) px = 2 * (w - 1) - px;
                    sum += gconv_data[0][(y * w + px) * channels + c] * gaussian_kernel[k + r];
                }

                gconv_data[1][(y * w + x) * channels + c] = sum * mul;
            }
        }
    }


    ////////// vertical blur
    kernel_reset_unifs(&gconv_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&gconv_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&gconv_k, q, w * (h - 4) * channels);
        kernel_load_unif(&gconv_k, q, q);

        kernel_load_unif(&gconv_k, q, w * channels);
        kernel_load_unif(&gconv_k, q, mul);

        kernel_load_unif(&gconv_k, q, TO_BUS(gconv_data[1] + offset + q * SIMD_WIDTH));
        kernel_load_unif(&gconv_k, q, TO_BUS(gconv_data[2] + offset + q * SIMD_WIDTH));
    }

    kernel_execute(&gconv_k);

    for (u32 y = 2; y < h - 2; y++) {
        for (u32 xi = 0; xi < 4; xi++) {
            u32 x = xs[xi];
            for (u32 c = 0; c < channels; c++) {
                float sum = 0.f;
                
                for (i32 k = -r; k <= r; k++) {
                    i32 py = y + k;
                    if (py < 0) py = -py;
                    if (py >= (i32) h) py = 2 * (h - 1) - py;
                    sum += gconv_data[1][(py * w + x) * channels + c] * gaussian_kernel[k + r];
                }

                gconv_data[2][(y * w + x) * channels + c] = sum * mul;
            }
        }
    }
    for (u32 yi = 0; yi < 4; yi++) {
        u32 y = ys[yi];
        for (u32 x = 0; x < w; x++) {
            for (u32 c = 0; c < channels; c++) {
                float sum = 0.f;
                
                for (i32 k = -r; k <= r; k++) {
                    i32 py = y + k;
                    if (py < 0) py = -py;
                    if (py >= (i32) h) py = 2 * (h - 1) - py;
                    sum += gconv_data[1][(py * w + x) * channels + c] * gaussian_kernel[k + r];
                }

                gconv_data[2][(y * w + x) * channels + c] = sum * mul;
            }
        }
    }

    img_like(out, in);
    memcpy(out->data, gconv_data[2], out->size * channels * sizeof(float));
}
static void downsample(Image* out, const Image* in) {
    Image temp;
    convolve_gaussian(&temp, in, 1.f);

    u32 nw = in->width / 2;
    u32 nh = in->height / 2;

    img_init(out, nw, nh, in->depth, in->fmt);
    for (u32 y = 0; y < nh; y++) {
        for (u32 x = 0; x < nw; x++) {
            for (u32 c = 0; c < in->fmt; c++) {
                img_set_data(out, y, x, c, img_get_data(&temp, y * 2, x * 2, c));
            }
        }
    }

    img_free(&temp);
}
static void upsample(Image* out, const Image* in, u32 width, u32 height) {
    Image temp;
    img_init(&temp, width, height, in->depth, in->fmt);

    for (u32 y = 0; y < in->height; y++) {
        for (u32 x = 0; x < in->width; x++) {
            for (u32 c = 0; c < in->fmt; c++) {
                img_set_data(&temp, y * 2, x * 2, c, img_get_data(in, y, x, c));
            }
        }
    }

    convolve_gaussian(out, &temp, 2.f);
    img_free(&temp);
}

static void compute_saturation_weight(Image* out, const Image* in) {
    assert(in->fmt == PIXEL_RGB, "saturation: needs RGB");

    img_gray_like(out, in);

    const float* data = in->data;
    for (u32 i = 0; i < in->size; i++) {
        float avg = (data[i * 3] + data[i * 3 + 1] + data[i * 3 + 2]) / 3.f;
        float dr = data[i * 3] - avg;
        float dg = data[i * 3 + 1] - avg;
        float db = data[i * 3 + 2] - avg;
        float std = sqrtf((dr * dr + dg * dg + db * db) / 3.f);
        out->data[i] = std;
    }
}
static void compute_contrast_weight(Image* out, const Image* in) {
    assert(in->fmt == PIXEL_RGB, "contrast: needs RGB");

    Image temp;
    img_to_grayscale(&temp, in);

    convolve_laplacian_abs(out, &temp);
    img_free(&temp);
}
static void compute_exposedness_weight(Image* out, const Image* in) {
    assert(in->fmt == PIXEL_RGB, "exposedness: needs RGB");

    const float sigma = 0.2f;
    const float exp_mul = -1.0f / (2.0f * sigma * sigma);

    img_gray_like(out, in);

    const float* data = in->data;
    for (u32 i = 0; i < in->size; i++) {
        float e = 1.f;
        for (u32 c = 0; c < 3; c++) {
            float val = data[i * 3 + c] - 0.5f;
            e *= expf(val * val * exp_mul);
        }

        out->data[i] = e;
    }
}
static void compute_weight_map(Image* out, const Image* in) {
    Image S, C, E;
    compute_saturation_weight(&S, in);
    compute_contrast_weight(&C, in);
    compute_exposedness_weight(&E, in);

    img_gray_like(out, in);
    for (u32 i = 0; i < in->size; i++) {
        out->data[i] = S.data[i] * C.data[i] * E.data[i];
    }

    img_free(&S);
    img_free(&C);
    img_free(&E);
}

static void normalize_weight_maps(MertensExposure* m, Image* weight_maps) {
    for (u32 i = 0; i < m->img_size; i++) {
        float sum = 0.f;
        for (u32 j = 0; j < m->num_imgs; j++) {
            sum += weight_maps[j].data[i];
        }
        if (sum >= F_EPS) {
            for (u32 j = 0; j < m->num_imgs; j++) {
                weight_maps[j].data[i] /= sum;
            }
        } else {
            for (u32 j = 0; j < m->num_imgs; j++) {
                weight_maps[j].data[i] = 1.f / m->num_imgs;
            }
        }
    }
}
static void build_gaussian_pyramid(MertensExposure* m, Image** out, const Image* in) {
    Image* pyr = kmalloc(m->num_lvls * sizeof(Image));

    img_copy(&pyr[0], in);
    for (u32 i = 1; i < m->num_lvls; i++) {
        downsample(&pyr[i], &pyr[i - 1]);
    }

    *out = pyr;
}
static void build_laplacian_pyramid(MertensExposure* m, Image** out, const Image* in) {
    Image* gauss;
    build_gaussian_pyramid(m, &gauss, in);

    Image* pyr = kmalloc(m->num_lvls * sizeof(Image));
    for (u32 i = 0; i < m->num_lvls - 1; i++) {
        Image temp;
        // allocates temp
        upsample(&temp, &gauss[i + 1], gauss[i].width, gauss[i].height);
        // allocates pyr[i]
        img_sub(&pyr[i], &gauss[i], &temp);
        img_free(&temp);
    }
    img_copy(&pyr[m->num_lvls - 1], &gauss[m->num_lvls - 1]);

    for (u32 i = 0; i < m->num_lvls; i++) {
        img_free(&gauss[i]);
    }

    *out = pyr;
}
static void blend_pyramids(MertensExposure* m, Image** out,
        Image** laplacians, Image** weights) {
    Image* pyr = kmalloc(m->num_lvls * sizeof(Image));
    for (u32 i = 0; i < m->num_lvls; i++) {
        // allocates pyr[i]
        img_mul(&pyr[i], &laplacians[0][i], &weights[0][i]);
        for (u32 j = 1; j < m->num_imgs; j++) {
            Image temp;
            img_mul(&temp, &laplacians[j][i], &weights[j][i]);
            u32 sz = pyr[i].size * pyr[i].fmt;
            for (u32 k = 0; k < sz; k++) { // don't wanna init again w/ img_sum
                pyr[i].data[k] += temp.data[k];
            }

            img_free(&temp);
        }
    }

    *out = pyr;
}
static void collapse_pyramid(MertensExposure* m, Image* out, const Image* pyramid) {
    img_copy(out, &pyramid[m->num_lvls - 1]);

    for (i32 i = m->num_lvls - 2; i >= 0; i--) {
        Image temp;
        upsample(&temp, out, pyramid[i].width, pyramid[i].height);

        img_free(out);
        img_add(out, &temp, &pyramid[i]);

        img_free(&temp);
    }
}

void mertens_init(MertensExposure* m, Image* imgs, u32 num_imgs) {
    assert(num_imgs > 0, "merten init: need at least one image");
    for (u32 i = 0; i < num_imgs; i++) {
        assert(imgs[i].fmt == PIXEL_RGB, "merten init: images have to be RGB");
    }

    if (!k_init) {
        arena_init_qpu(&arena, arena_size);
        kernel_init(&gconv_k, NUM_QPUS, 7, gaussian_conv, sizeof(gaussian_conv));

        for (u32 i = 0; i < 3; i++) {
            gconv_data[i] = arena_alloc_align(&arena, img_max_size, 16 * sizeof(float));
        }

        k_init = true;
    }

    m->width = imgs[0].width;
    m->height = imgs[0].height;
    m->depth = imgs[0].depth;
    m->img_size = imgs[0].size;
    m->num_imgs = num_imgs;
    m->num_lvls = max(1, (u32) (logf(1.f * min(m->width, m->height)) / LN2) - 2);
    m->imgs = imgs;
    m->pmax = (1 << m->depth) - 1;
}


#if VERBOSE
static uint32_t last_t;
#define now(...) \
    do { \
        printk(__VA_ARGS__); \
        last_t = sys_timer_get_usec(); \
    } while(false)
#define elapsed() printk("elapsed (us): %d\n", sys_timer_get_usec() - last_t)
#else
#define now(...)
#define elapsed()
#endif

Image* mertens_fuse(MertensExposure* m) {
    now("normalizing...\n");
    for (u32 i = 0; i < m->num_imgs; i++) {
        float* data = m->imgs[i].data;
        for (u32 j = 0; j < m->img_size * 3; j++) {
            data[j] /= m->pmax;
        }
    }
    elapsed();


    Image* weight_maps = kmalloc(m->num_imgs * sizeof(Image));
    now("computing weight maps...\n");
    for (u32 i = 0; i < m->num_imgs; i++) {
        compute_weight_map(&weight_maps[i], &m->imgs[i]);
    }
    elapsed();

    now("normalizing weight maps...\n");
    normalize_weight_maps(m, weight_maps);
    elapsed();

    Image** laplacians = kmalloc(m->num_imgs * sizeof(Image*));
    Image** weights = kmalloc(m->num_imgs * sizeof(Image*));
    now("building pyramids...\n");
    for (u32 i = 0; i < m->num_imgs; i++) {
        build_laplacian_pyramid(m, &laplacians[i], &m->imgs[i]);
        build_gaussian_pyramid(m, &weights[i], &weight_maps[i]);
    }
    elapsed();

    // free weight maps
    for (u32 i = 0; i < m->num_imgs; i++) {
        img_free(&weight_maps[i]);
    }


    Image* blended = kmalloc(m->num_lvls * sizeof(Image));
    now("blending...\n");
    blend_pyramids(m, &blended, laplacians, weights);
    elapsed();

    // free laplacians and gaussian pyramids
    for (u32 i = 0; i < m->num_imgs; i++) {
        for (u32 j = 0; j < m->num_lvls; j++) {
            img_free(&laplacians[i][j]);
            img_free(&weights[i][j]);
        }
    }
    kfree(laplacians);
    kfree(weights);


    Image* out = kmalloc(sizeof(Image));
    now("collapsing...\n");
    collapse_pyramid(m, out, blended);
    elapsed();


    now("scaling back...\n");
    for (u32 i = 0; i < m->img_size * 3; i++) {
        out->data[i] = clampf(out->data[i] * m->pmax, 0.f, (float) m->pmax);
    }
    elapsed();

    // free blended pyramid
    for (u32 i = 0; i < m->num_lvls; i++) {
        img_free(&blended[i]);
    }

    return out;
}

