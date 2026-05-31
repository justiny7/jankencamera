#include "lib.h"
#include "camera.h"
#include "display.h"
#include "mmu.h"
#include "sys_timer.h"

#define FB_WIDTH  640
#define FB_HEIGHT 480
// #define FB_WIDTH  1920
// #define FB_HEIGHT 1080

#define N 4

static CameraBuffer bufs[N];

// Bilinear debayer for 16-bit RGGB Bayer pattern
static void debayer(uint16_t* bayer, uint32_t* rgb, uint32_t w, uint32_t h, 
                    uint32_t in_stride_bytes, uint32_t out_stride_pixels) {
    uint32_t stride = in_stride_bytes / 2;
    // printk("stride: %d\nout stride: %d\n", stride, out_stride_pixels);
    
    uint32_t ravg = 0;
    uint32_t gavg = 0;
    uint32_t bavg = 0;
    
    uint32_t skip = 1;
    for (uint32_t y = 1; y < h - 1; y += skip) {
        for (uint32_t x = 1; x < w - 1; x += skip) {
            uint16_t* p = bayer + y * stride + x;
            // /*
            uint32_t r, g, b;

            if ((y & 1) == 0) {
                if ((x & 1) == 0) {
                    r = p[0];
                    g = (p[-1] + p[1] + p[-(int)stride] + p[stride]) >> 2;
                    b = (p[-(int)stride-1] + p[-(int)stride+1] + p[stride-1] + p[stride+1]) >> 2;
                } else {
                    r = (p[-1] + p[1]) >> 1;
                    g = p[0];
                    b = (p[-(int)stride] + p[stride]) >> 1;
                }
            } else {
                if ((x & 1) == 0) {
                    r = (p[-(int)stride] + p[stride]) >> 1;
                    g = p[0];
                    b = (p[-1] + p[1]) >> 1;
                } else {
                    r = (p[-(int)stride-1] + p[-(int)stride+1] + p[stride-1] + p[stride+1]) >> 2;
                    g = (p[-1] + p[1] + p[-(int)stride] + p[stride]) >> 2;
                    b = p[0];
                }
            }

            r = r >> 2;
            g = g >> 2;
            b = b >> 2;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;

            rgb[y * out_stride_pixels/skip + x/skip] = (r << 16) | (g << 8) | b;
            // */

            // {
                // uint32_t v = (p[0] >> 2);
                // rgb[y * out_stride_pixels/skip + x/skip] = (v << 16) | (v << 8) | v;
            // }

            /*
            ravg += r;
            bavg += b;
            gavg += g;
            */
        }
    }

    /*
    ravg /= w * h;
    gavg /= w * h;
    bavg /= w * h;
    uint32_t tavg = (ravg + gavg + bavg) / 3;
    printk("r: %d\ng: %d\nb: %d\ntot: %d\n", ravg, gavg, bavg, tavg);
    */
}

void main() {
    mmu_enable_caches();

    printk("jankencamera starting...\n");

    if (!display_init(FB_WIDTH, FB_HEIGHT)) {
        printk("display init failed\n");
        return;
    }

    if (!camera_init()) {
        printk("camera init failed\n");
        return;
    }

    if (!camera_set_format(FB_WIDTH, FB_HEIGHT, CAM_FMT_BAYER_10)) {
        printk("camera set format failed\n");
        return;
    }

    // int expos[] = { 3200, 6000, 9000, 15000 };
    // int expos[] = { 50000, 100000, 149999, 250000 };
    // int expos[] = { 60000, 180000, 240000, 360000 };
    // int expos[] = { 60000, 60000, 60000, 60000 };
    int expos[] = { 10000, 10000, 10000, 10000 };
    // int expos[] = { 100, 1000, 10000, 100000 };
    // int expos[] = { 1000, 5000, 10000, 50000 };
    float gain[] = { 1.0, 2.0, 4.0, 8.0 };
    float gain2[] = { 4.0, 8.0, 64.0, 128.0 };
    // float gain2[] = { 4.0, 4.0, 4.0, 4.0 };
    float ag[] = { 1.0, 2.0, 4.0, 8.0 };
    // float dg[] = { 1.0, 2.0, 4.0, 8.0 };
    float dg[] = { 2.0, 4.0, 8.0, 16.0 };
    int c = 0;

    // camera_set_exposure(880);
    camera_set_exposure(10000);
    // camera_set_exposure(60000);
    // camera_set_exposure(100000);
    camera_set_gain(8.0);
    // camera_set_gain(4.0);
    // camera_set_gain(128.0);
    // camera_set_exposure(4000);
    // camera_set_gain(180);
    // camera_set_analog_gain(8.0);
    // camera_set_digital_gain(1.0);
    // camera_set_digital_gain(2175);

    if (!camera_start()) {
        printk("camera start failed\n");
        return;
    }

    CameraConfig cfg = camera_get_config();
    DisplayConfig disp = display_get_config();
    printk("streaming %dx%d\n", cfg.width, cfg.height);

    /*
    const int SEC = 1000000 / 4;
    int ts[] = { 0, SEC * 1, SEC * 2, SEC * 3 };
    CameraFrame frames[N];
    CameraShot shots[N];
    for (int i = 0; i < N; i++) {
        frames[i].buf = &bufs[i];
        shots[i].ts = ts[i];
        shots[i].gain = gain2[i];
        shots[i].exposure = expos[i];
    }

    mmu_flush_dcache();

    uint32_t cap = camera_capture_frames(frames, shots, N);
    printk("cap: %d\n", cap);
    if (cap != N) {
        rpi_reset();
    }

    for (int i = 0; ; i++) {
        int j = i % N;

        printk("displaying buf %x\n", frames[j].buf);
        debayer((uint16_t*) frames[j].buf, display_get_buffer(),
                cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
        display_swap();

        CameraFrame frame = frames[j];
        printk("expos: %d\ngain: %f\nts: %d\ndelay: %d\nerror: %d\n",
                frame.shot.exposure, frame.shot.gain, frame.shot.ts, frame.shot.delay, frame.shot.error);
        sys_timer_delay_ms(1000);
    }
    // */

    CameraFrame frame;
    while (1) {
        if (camera_capture_frame(&frame)) {
            // printk("got %d\n", sys_timer_get_usec());
            debayer((uint16_t*) frame.buf, display_get_buffer(),
                    cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
            display_swap();
        }
        // printk("$\n");

        /*
        if (frame.sequence && frame.sequence % 50 == 0) {
            CameraShot shot = {
                .gain = 0,
                .exposure = 0,
                .ana_gain = 8.0,
                .dig_gain = 1.0,
            };
            if (camera_capture_frame_shot(&frame, shot)) {
                debayer((uint16_t*) frame.data, display_get_buffer(),
                        cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
                printk("expos: %d\tana gain:%f\tdig gain:%f\n",
                        frame.shot.exposure, frame.shot.ana_gain, frame.shot.dig_gain);
                display_swap();
            }

            sys_timer_delay_sec(2);

            shot.ana_gain = 1.0;
            shot.dig_gain = 8.0;
            if (camera_capture_frame_shot(&frame, shot)) {
                debayer((uint16_t*)frame.data, display_get_buffer(),
                        cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
                printk("expos: %d\tana gain:%f\tdig gain:%f\n",
                        frame.shot.exposure, frame.shot.ana_gain, frame.shot.dig_gain);
                display_swap();
            }

            sys_timer_delay_sec(2);
        }
        */


        /*
        if (frame.sequence && frame.sequence % 50 == 0) {
            for (int i = 0; i < 4; i++) {
                c = (c + 1) % 4;
                CameraShot shot = {
                    // .exposure = expos[c],
                    .exposure = 0,
                    // .dig_gain = dg[c],
                    // .dig_gain = 0,
                    // .ana_gain = ag[c]
                    // .ana_gain = 0
                    .gain = gain2[c],
                };
                if (camera_capture_frame_shot(&frame, shot)) {
                    debayer((uint16_t*)frame.buf, display_get_buffer(),
                            cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
                    printk("expos: %d\tana gain:%f\tdig gain:%f\n",
                            frame.shot.exposure, frame.shot.ana_gain, frame.shot.dig_gain);
                    display_swap();
                }
                sys_timer_delay_ms(500);
            }
        }
        */
    }

    camera_stop();
}
