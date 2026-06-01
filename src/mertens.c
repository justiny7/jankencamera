#include "mertens.h"
#include "math.h"
#include "kmem.h"
#include "lib.h"
#include "sys_timer.h"
#include "mmu.h"

#include "kernel.h"
#include "gaussian_conv.h"
#include "sat_expos_grayscale.h"
#include "laplacian_conv.h"

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

// use GPU arenas even for faster alloc/dealloc
static const u32 arena_size = 100 * 1024 * 1024;
static const u32 arena_align = 16 * sizeof(float);
static Arena data_arena, temp_arena;
static Kernel gconv_k, sat_expos_grayscale_k, lconv_k;
static bool k_init;

static void convolve_gaussian(Image* out, const Image* in, float mul) {
    i32 r = gaussian_kernel_size / 2;

    u32 w = in->width;
    u32 h = in->height;
    u32 channels = in->fmt;

    u32 offset = w * channels * 2; // 2-pixel margin to handle borders

    img_like(out, in);

    u32 temp_sz = temp_arena.size;
    Image temp = (Image) {
        .data = arena_alloc_align(&temp_arena, img_nbytes(in), arena_align)
    };
    img_like(&temp, in);

    ////////// horizontal blur
    kernel_reset_unifs(&gconv_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&gconv_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&gconv_k, q, w * (h - 4) * channels);
        kernel_load_unif(&gconv_k, q, q);

        kernel_load_unif(&gconv_k, q, 1 * channels);
        kernel_load_unif(&gconv_k, q, mul);

        kernel_load_unif(&gconv_k, q, TO_BUS(in->data + offset + q * SIMD_WIDTH));
        kernel_load_unif(&gconv_k, q, TO_BUS(temp.data + offset + q * SIMD_WIDTH));
    }

    kernel_execute(&gconv_k);

    const u32 ys[4] = { 0, 1, h - 2, h - 1 };
    const u32 xs[4] = { 0, 1, w - 2, w - 1 };

    for (u32 yi = 0; yi < 4; yi++) {
        u32 y = ys[yi];
        for (u32 x = 0; x < w; x++) {
            for (u32 c = 0; c < channels; c++) {
                float sum = 0.f;
                
                for (i32 k = -r; k <= r; k++) {
                    i32 px = x + k;
                    if (px < 0) px = -px;
                    if (px >= (i32) w) px = 2 * (w - 1) - px;
                    sum += in->data[(y * w + px) * channels + c] * gaussian_kernel[k + r];
                }

                temp.data[(y * w + x) * channels + c] = sum * mul;
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
                    sum += in->data[(y * w + px) * channels + c] * gaussian_kernel[k + r];
                }

                temp.data[(y * w + x) * channels + c] = sum * mul;
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

        kernel_load_unif(&gconv_k, q, TO_BUS(temp.data + offset + q * SIMD_WIDTH));
        kernel_load_unif(&gconv_k, q, TO_BUS(out->data + offset + q * SIMD_WIDTH));
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
                    sum += temp.data[(py * w + x) * channels + c] * gaussian_kernel[k + r];
                }

                out->data[(y * w + x) * channels + c] = sum * mul;
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
                    sum += temp.data[(py * w + x) * channels + c] * gaussian_kernel[k + r];
                }

                out->data[(y * w + x) * channels + c] = sum * mul;
            }
        }
    }

    arena_dealloc_to(&temp_arena, temp_sz);
    mmu_flush_dcache();
}
static void downsample(Image* out, const Image* in, Arena* arena) {
    const u32 nw = in->width / 2;
    const u32 nh = in->height / 2;
    out->data = arena_alloc_align(arena,
            nw * nh * in->fmt * sizeof(float), arena_align);
    img_init(out, nw, nh, in->depth, in->fmt);

    u32 temp_sz = temp_arena.size;
    Image temp = (Image) {
        .data = arena_alloc_align(&temp_arena, img_nbytes(in), arena_align)
    };
    convolve_gaussian(&temp, in, 1.f);
    for (u32 y = 0; y < nh; y++) {
        for (u32 x = 0; x < nw; x++) {
            for (u32 c = 0; c < in->fmt; c++) {
                img_set_data(out, y, x, c, img_get_data(&temp, y * 2, x * 2, c));
            }
        }
    }

    arena_dealloc_to(&temp_arena, temp_sz);
    mmu_flush_dcache();
}
static void upsample(Image* out, const Image* in, u32 width, u32 height, Arena* arena) {
    out->data = arena_alloc_align(arena,
            width * height * in->fmt * sizeof(float), arena_align);

    u32 temp_sz = temp_arena.size;
    Image temp = (Image) {
        .data = arena_alloc_align(arena,
                width * height * in->fmt * sizeof(float), arena_align)
    };
    img_init(&temp, width, height, in->depth, in->fmt);
    memset(temp.data, 0, img_nbytes(&temp));

    for (u32 y = 0; y < in->height; y++) {
        for (u32 x = 0; x < in->width; x++) {
            for (u32 c = 0; c < in->fmt; c++) {
                img_set_data(&temp, y * 2, x * 2, c, img_get_data(in, y, x, c));
            }
        }
    }

    convolve_gaussian(out, &temp, 2.f);
    arena_dealloc_to(&temp_arena, temp_sz);
    mmu_flush_dcache();
}
static void compute_weight_map(Image* out, const Image* in) {
    assert(in->fmt == PIXEL_RGB, "compute weight maps: needs RGB");

    out->data = arena_alloc_align(&data_arena, in->size * sizeof(float), arena_align);
    img_gray_like(out, in);

    // for contrast
    u32 temp_sz = temp_arena.size;
    Image temp = (Image) {
        .data = arena_alloc_align(&temp_arena, img_nbytes(out), arena_align)
    };
    img_gray_like(&temp, in);

    ////// calculate saturation, exposedness, and convert img to grayscale
    kernel_reset_unifs(&sat_expos_grayscale_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&sat_expos_grayscale_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&sat_expos_grayscale_k, q, out->size);
        kernel_load_unif(&sat_expos_grayscale_k, q, q);

        kernel_load_unif(&sat_expos_grayscale_k, q, TO_BUS(in->data));
        kernel_load_unif(&sat_expos_grayscale_k, q, TO_BUS(temp.data + q * SIMD_WIDTH));
        kernel_load_unif(&sat_expos_grayscale_k, q, TO_BUS(out->data + q * SIMD_WIDTH));
    }

    kernel_execute(&sat_expos_grayscale_k);

    mmu_flush_dcache();

    // save border pixels
    u32 w = in->width;
    u32 h = in->height;
    float* row_border[2] = {
        kmalloc(w * sizeof(float)),
        kmalloc(w * sizeof(float)),
    };
    float* col_border[2] = {
        kmalloc(h * sizeof(float)),
        kmalloc(h * sizeof(float)),
    };

    const u32 ys[2] = { 0, h - 1 };
    const u32 xs[2] = { 0, w - 1 };
    for (u32 y = 0; y < h; y++) {
        for (u32 xi = 0; xi < 2; xi++) {
            u32 x = xs[xi];
            col_border[xi][y] = out->data[y * w + x];
        }
    }
    for (u32 yi = 0; yi < 2; yi++) {
        u32 y = ys[yi];
        for (u32 x = 1; x < w - 1; x++) {
            row_border[yi][x] = out->data[y * w + x];
        }
    }

    ////// laplacian conv for contrast
    kernel_reset_unifs(&lconv_k);
    for (int q = 0; q < NUM_QPUS; q++) {
        kernel_load_unif(&lconv_k, q, NUM_QPUS * SIMD_WIDTH);
        kernel_load_unif(&lconv_k, q, w * (h - 2));
        kernel_load_unif(&lconv_k, q, q);

        kernel_load_unif(&lconv_k, q, 1);
        kernel_load_unif(&lconv_k, q, w);

        // 1-pixel margin to handle borders
        kernel_load_unif(&lconv_k, q, TO_BUS(temp.data + w + q * SIMD_WIDTH));
        kernel_load_unif(&lconv_k, q, TO_BUS(out->data + w + q * SIMD_WIDTH));
    }

    kernel_execute(&lconv_k);

    for (u32 y = 0; y < h; y++) {
        for (u32 xi = 0; xi < 2; xi++) {
            u32 x = xs[xi];

            u32 xl = (x == 0 ? 1 : x - 1);
            u32 xr = (x == w - 1 ? w - 2 : x + 1);
            u32 yl = (y == 0 ? 1 : y - 1);
            u32 yr = (y == h - 1 ? h - 2 : y + 1);
            float C = img_get_data(&temp, y, xl, 0) +
                img_get_data(&temp, y, xr, 0) +
                img_get_data(&temp, yl, x, 0) +
                img_get_data(&temp, yr, x, 0) -
                img_get_data(&temp, y, x, 0) * 4.f;

            if (C < 0) C = -C;

            out->data[y * w + x] = col_border[xi][y] * C;
        }
    }
    for (u32 yi = 0; yi < 2; yi++) {
        u32 y = ys[yi];

        for (u32 x = 1; x < w - 1; x++) {
            u32 xl = (x == 0 ? 1 : x - 1);
            u32 xr = (x == w - 1 ? w - 2 : x + 1);
            u32 yl = (y == 0 ? 1 : y - 1);
            u32 yr = (y == h - 1 ? h - 2 : y + 1);
            float C = img_get_data(&temp, y, xl, 0) +
                img_get_data(&temp, y, xr, 0) +
                img_get_data(&temp, yl, x, 0) +
                img_get_data(&temp, yr, x, 0) -
                img_get_data(&temp, y, x, 0) * 4.f;

            if (C < 0) C = -C;

            out->data[y * w + x] = row_border[yi][x] * C;
        }
    }

    for (u32 i = 0; i < 2; i++) {
        kfree(row_border[i]);
        kfree(col_border[i]);
    }

    arena_dealloc_to(&temp_arena, temp_sz);
    mmu_flush_dcache();
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
    mmu_flush_dcache();
}
static void build_gaussian_pyramid(MertensExposure* m, Image** out, const Image* in,
        Arena* arena) {
    Image* pyr = img_kmalloc_n(m->num_lvls);

    pyr[0].data = arena_alloc_align(arena, img_nbytes(in), arena_align);
    img_copy(&pyr[0], in);
    for (u32 i = 1; i < m->num_lvls; i++) {
        downsample(&pyr[i], &pyr[i - 1], arena);
    }

    *out = pyr;
    mmu_flush_dcache();
}
static void build_laplacian_pyramid(MertensExposure* m, Image** out, const Image* in) {
    u32 temp_sz = temp_arena.size;
    Image* gauss;
    build_gaussian_pyramid(m, &gauss, in, &temp_arena);

    Image* pyr = img_kmalloc_n(m->num_lvls);
    Image temp;
    for (u32 i = 0; i < m->num_lvls - 1; i++) {
        u32 temp_sz2 = temp_arena.size;
        upsample(&temp, &gauss[i + 1], gauss[i].width, gauss[i].height, &temp_arena);

        pyr[i].data = arena_alloc_align(&data_arena, img_nbytes(&temp), arena_align);
        img_sub(&pyr[i], &gauss[i], &temp);

        arena_dealloc_to(&temp_arena, temp_sz2);
    }
    pyr[m->num_lvls - 1].data = arena_alloc_align(&data_arena, img_nbytes(&temp), arena_align);
    img_copy(&pyr[m->num_lvls - 1], &gauss[m->num_lvls - 1]);

    kfree(gauss);

    *out = pyr;
    arena_dealloc_to(&temp_arena, temp_sz);
    mmu_flush_dcache();
}
static void blend_pyramids(MertensExposure* m, Image** out,
        Image** laplacians, Image** weights) {
    Image* pyr = img_kmalloc_n(m->num_lvls);
    for (u32 i = 0; i < m->num_lvls; i++) {
        u32 sz = img_nbytes(&laplacians[0][i]);
        pyr[i].data = arena_alloc_align(&data_arena, sz, arena_align);
        memset(pyr[i].data, 0, sz);

        img_like(&pyr[i], &laplacians[0][i]);
        for (u32 j = 0; j < m->num_imgs; j++) {
            img_mul_add(&pyr[i], &laplacians[j][i], &weights[j][i]);
        }
    }

    *out = pyr;
    mmu_flush_dcache();
}
static void collapse_pyramid(MertensExposure* m, Image* out, const Image* pyramid) {
    u32 data_sz = data_arena.size;
    u32 temp_sz = temp_arena.size;

    out->data = arena_alloc_align(&data_arena,
            img_nbytes(&pyramid[m->num_lvls - 1]), arena_align);
    img_copy(out, &pyramid[m->num_lvls - 1]);

    Image temp;
    for (i32 i = m->num_lvls - 2; i >= 0; i--) {
        upsample(&temp, out, pyramid[i].width, pyramid[i].height, &temp_arena);

        arena_dealloc_to(&data_arena, data_sz);
        out->data = arena_alloc_align(&data_arena, img_nbytes(&pyramid[i]), arena_align);
        img_add(out, &temp, &pyramid[i]);

        arena_dealloc_to(&temp_arena, temp_sz);
    }

    mmu_flush_dcache();
}

void mertens_init(MertensExposure* m, Image* imgs, u32 num_imgs) {
    assert(num_imgs > 0, "merten init: need at least one image");
    for (u32 i = 0; i < num_imgs; i++) {
        assert(imgs[i].fmt == PIXEL_RGB, "merten init: images have to be RGB");
    }

    if (!k_init) {
        img_kernel_init();

        arena_init_qpu(&data_arena, arena_size);
        arena_init_qpu(&temp_arena, arena_size);
        kernel_init(&gconv_k, NUM_QPUS,
                7, gaussian_conv, sizeof(gaussian_conv));
        kernel_init(&sat_expos_grayscale_k, NUM_QPUS,
                7, sat_expos_grayscale, sizeof(sat_expos_grayscale));
        kernel_init(&lconv_k, NUM_QPUS,
                7, laplacian_conv, sizeof(laplacian_conv));

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

    mmu_flush_dcache();
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

Image* mertens_fuse(MertensExposure* m, Image* out) {
    now("normalizing...\n");
    for (u32 i = 0; i < m->num_imgs; i++) {
        img_mul_scalar_clamp(&m->imgs[i], 1.f / m->pmax, 0.f, 1.f);
    }
    elapsed();

    Image* weight_maps = img_kmalloc_n(m->num_imgs);
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
        build_gaussian_pyramid(m, &weights[i], &weight_maps[i], &data_arena);
    }
    elapsed();


    Image* blended = img_kmalloc_n(m->num_lvls);
    now("blending...\n");
    blend_pyramids(m, &blended, laplacians, weights);
    elapsed();

    Image* out_qpu = img_kmalloc();
    now("collapsing...\n");
    collapse_pyramid(m, out_qpu, blended);
    elapsed();

    now("scaling back...\n");
    img_mul_scalar_clamp(out_qpu, (float) m->pmax, 0.f, (float) m->pmax);
    elapsed();

    // copy to CPU
    if (!out) out = img_kmalloc();
    img_copy(out, out_qpu);

    printk("Data arena usage: %d bytes (%f MiB)\n",
            data_arena.size, 1.f * data_arena.size / (1024 * 1024));

    // free everything
    kfree(weight_maps);
    for (u32 i = 0; i < m->num_imgs; i++) {
        kfree(laplacians[i]);
        kfree(weights[i]);
    }
    kfree(laplacians);
    kfree(weights);
    kfree(blended);
    kfree(out_qpu);

    arena_dealloc_to(&data_arena, 0);
    arena_dealloc_to(&temp_arena, 0);

    for (u32 i = 0; i < m->num_imgs; i++) {
        img_mul_scalar_clamp(&m->imgs[i], m->pmax, 0.f, m->pmax);
    }

    return out;
}

