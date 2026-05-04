#include "lib.h"
#include "camera.h"
#include "display.h"
#include "mmu.h"

#define FB_WIDTH  640
#define FB_HEIGHT 480

// Bilinear debayer for 16-bit RGGB Bayer pattern
static void debayer(uint16_t* bayer, uint32_t* rgb, uint32_t w, uint32_t h, 
                    uint32_t in_stride_bytes, uint32_t out_stride_pixels) {
    uint32_t stride = in_stride_bytes / 2;
    // printk("stride: %d\nout stride: %d\n", stride, out_stride_pixels);
    
    uint32_t ravg = 0;
    uint32_t gavg = 0;
    uint32_t bavg = 0;
    
    for (uint32_t y = 1; y < h - 1; y++) {
        for (uint32_t x = 1; x < w - 1; x++) {
            uint16_t* p = bayer + y * stride + x;
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

            rgb[y * out_stride_pixels + x] = (r << 16) | (g << 8) | b;

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

    // camera_set_exposure(880);
    camera_set_exposure(3200);
    // camera_set_gain(180);
    camera_set_analog_gain(116);
    camera_set_digital_gain(2175);

    if (!camera_start()) {
        printk("camera start failed\n");
        return;
    }

    CameraConfig cfg = camera_get_config();
    DisplayConfig disp = display_get_config();
    printk("streaming %dx%d\n", cfg.width, cfg.height);

    CameraFrame frame;
    while (1) {
        if (camera_capture_frame(&frame)) {
            debayer((uint16_t*)frame.data, display_get_buffer(),
                    cfg.width, cfg.height, cfg.stride, disp.pitch / 4);
            camera_release_frame(&frame);
            display_swap();
        }
    }

    camera_stop();
}
