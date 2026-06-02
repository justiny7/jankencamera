#include "lib.h"
#include "camera.h"
#include "display.h"
#include "mmu.h"
#include "sys_timer.h"
#include "uart.h"
#include "unicam.h"
#include "mertens.h"
#include "fat.h"

#define FB_WIDTH  640
#define FB_HEIGHT 480

#define N 3
#define DELAY (500 * 1000) // 500ms
static CameraBuffer bufs[N];
static CameraFrame frames[N];
static CameraShot shots[N];

typedef enum {
    STREAMING,
    MERTENS,
} Mode;

static volatile Mode cur_mode = STREAMING;

static uint32_t cur_expos = 20000;
static float cur_gain = 4.0;

// streaming settings
static volatile bool settings_change;

// mertens settings
static uint32_t mertens_expos[N] = { 5000, 10000, 20000 };
static float mertens_gain[N] = { 4.0, 4.0, 8.0 };
static volatile bool mertens_launch;
static volatile bool mertens_save;
static uint32_t mertens_change;
static uint32_t mertens_cur_img;
static uint32_t mertens_save_idx;
static void __attribute__((interrupt("IRQ"))) irq_handler() {
    if (unicam_irq_pending()) {
        unicam_irq_handler();
    } else {
        uint8_t c;
        if (uart_get_interrupt_char(&c)) {
            if (cur_mode == STREAMING) {
                switch (c) {
                    case 'w':
                        if (cur_expos < 10000) {
                            cur_expos += 1000;
                            settings_change = true;
                        } else if (cur_expos < 1000000) {
                            cur_expos += 5000;
                            settings_change = true;
                        }
                        break;
                    case 's':
                        if (cur_expos > 10000) {
                            cur_expos -= 5000;
                            settings_change = true;
                        } else if (cur_expos > 1000) {
                            cur_expos -= 1000;
                            settings_change = true;
                        }
                        break;
                    case 'e':
                        if (cur_gain < 128.0) {
                            cur_gain *= 2.0;
                            settings_change = true;
                        }
                        break;
                    case 'd':
                        if (cur_gain > 1.0) {
                            cur_gain *= 0.5;
                            settings_change = true;
                        }
                        break;
                    case '1':
                    case '2':
                    case '3':
                        mertens_change = c - '0';
                        mertens_expos[mertens_change - 1] = cur_expos;
                        mertens_gain[mertens_change - 1] = cur_gain;
                        break;
                    case '!':
                        cur_expos = mertens_expos[0];
                        cur_gain = mertens_gain[0];
                        settings_change = true;
                        break;
                    case '@':
                        cur_expos = mertens_expos[1];
                        cur_gain = mertens_gain[1];
                        settings_change = true;
                        break;
                    case '#':
                        cur_expos = mertens_expos[2];
                        cur_gain = mertens_gain[2];
                        settings_change = true;
                        break;
                    case 'm':
                        cur_mode = MERTENS;
                        mertens_launch = true;
                        break;
                    case 'q':
                        rpi_reset();
                    default:
                        break;
                }
            } else {
                switch (c) {
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                        mertens_cur_img = c - '0' - 1;
                        mertens_change = true;
                        break;
                    case 'a':
                        mertens_cur_img = (mertens_cur_img + N * 2) % (N * 2 + 1);
                        mertens_change = true;
                        break;
                    case 'd':
                        mertens_cur_img = (mertens_cur_img + 1) % (N * 2 + 1);
                        mertens_change = true;
                        break;
                    case 'q':
                        cur_mode = STREAMING;
                        settings_change = true;
                        mertens_change = 0;
                        break;
                    case 's':
                        mertens_save = true;
                    default:
                        break;
                }
            }
        }
    }

    mmu_flush_dcache();
}

void main() {
    // assert(N > 1 && N < 9, "choose between 2-8 images");
    assert(N == 3, "hardcoded 3 images");

    mmu_enable_caches();

    extern uint32_t irq_ptr;
    irq_ptr = (uint32_t) irq_handler;

    fat_init();
    uart_flush_tx();
    uart_clear_fifos();
    uart_enable_rx_interrupts();
    mem_barrier_dsb();

    printk("jankencamera starting...\n");

    if (!display_init(FB_WIDTH, FB_HEIGHT)) {
        printk("display init failed\n");
        return;
    }

    if (!camera_init(false)) {
        printk("camera init failed\n");
        return;
    }

    if (!camera_set_format(FB_WIDTH, FB_HEIGHT, CAM_FMT_BAYER_10)) {
        printk("camera set format failed\n");
        return;
    }

    camera_set_exposure(cur_expos);
    camera_set_gain(cur_gain);

    if (!camera_start()) {
        printk("camera start failed\n");
        return;
    }

    for (uint32_t i = 0; i < N; i++) {
        frames[i].buf = &bufs[i];
        shots[i].ts = DELAY * i;
    }

    CameraConfig cfg = camera_get_config();
    DisplayConfig disp = display_get_config();
    printk("streaming %dx%d\n", cfg.width, cfg.height);

    img_kernel_init();
    CameraFrame frame;
    Image* img = img_kmalloc();
    Image* imgs = img_kmalloc_n(N * 2 + 1);
    while (1) {
        if (cur_mode == STREAMING) {
            if (camera_capture_frame(&frame)) {
                img_init_frame(img, &frame);

                img_bw_norm_gray_world_wb(img, 1023, 64);
                img_debayer_fast(img, display_get_buffer(), false);

                display_swap();
            }

            if (settings_change) {
                camera_set_exposure(cur_expos);
                camera_set_gain(cur_gain);
                printk("update camera settings - expos: %d, gain: %f\n", cur_expos, cur_gain);
                settings_change = false;
            }
            if (mertens_change) {
                printk("update settings for exposure frame %d - exposure: %d, gain: %f\n",
                        mertens_change, mertens_expos[mertens_change - 1], mertens_gain[mertens_change - 1]);
                mertens_change = 0;
            }
        } else {
            if (mertens_launch) {
                uart_disable_interrupts();
                for (uint32_t i = 0; i < N; i++) {
                    shots[i].exposure = mertens_expos[i];
                    shots[i].gain = mertens_gain[i];
                }

                mmu_flush_dcache();

                uint32_t cap = camera_capture_frames(frames, shots, N);
                printk("captured %d shots\n", cap);
                if (cap != N) {
                    printk("error - couldn't capture right shots\n");
                    cur_mode = STREAMING;
                    continue;
                }

                printk("processing shots...\n");
                for (uint32_t i = 0; i < N; i++) {
                    img_init_frame(&imgs[i], &frames[i]);
                    img_copy(&imgs[i + N], &imgs[i]);
                    img_bw_norm_gray_world_wb(&imgs[i + N], 1023, 64);
                    img_debayer_fast(&imgs[i + N], 0, true);
                }

                printk("performing exposure fusion...\n");
                MertensExposure m;
                mertens_init(&m, imgs + N, N);
                mertens_fuse(&m, &imgs[N * 2]);

                mertens_cur_img = 0;
                printk("displaying frame %d\n", mertens_cur_img);
                img_write_framebuffer(&imgs[mertens_cur_img], display_get_buffer());
                display_swap();

                mertens_launch = false;
                mertens_change = 0;
                uart_enable_rx_interrupts();
            }

            if (mertens_change) {
                printk("displaying frame %d\n", mertens_cur_img);
                img_write_framebuffer(&imgs[mertens_cur_img], display_get_buffer());
                display_swap();
                mertens_change = false;
            }

            if (mertens_save) {
                uart_disable_interrupts();
                for (uint32_t i = 0; i < N; i++) {
                    printk("saving bay_%d_%d.ppm...\n", i, mertens_save_idx);
                    char* fn = "BAY_x_x PPM";
                    fn[4] = '0' + i;
                    fn[6] = '0' + mertens_save_idx;
                    img_save_ppm(&imgs[i], fn);
                }
                for (uint32_t i = 0; i < N; i++) {
                    printk("saving in_%d_%d.ppm...\n", i, mertens_save_idx);
                    char* fn = "IN_x_x  PPM";
                    fn[3] = '0' + i;
                    fn[5] = '0' + mertens_save_idx;
                    img_save_ppm(&imgs[i + N], fn);
                }
                {
                    printk("saving out_%d.ppm...\n", mertens_save_idx);
                    char* fn = "OUT_x   PPM";
                    fn[4] = '0' + mertens_save_idx;
                    img_save_ppm(&imgs[N * 2], fn);
                }
                printk("done!\n");
                mertens_save = false;
                mertens_save_idx++;
                uart_enable_rx_interrupts();
            }
        }
    }

    camera_stop();
}
