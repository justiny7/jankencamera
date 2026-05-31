#include "lib.h"
#include "camera.h"
#include "display.h"
#include "mmu.h"
#include "sys_timer.h"
#include "image.h"

#define FB_WIDTH  640
#define FB_HEIGHT 480
// #define FB_WIDTH  1920
// #define FB_HEIGHT 1080

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

    camera_set_exposure(30000);
    camera_set_gain(32.0);

    if (!camera_start()) {
        printk("camera start failed\n");
        return;
    }

    CameraConfig cfg = camera_get_config();
    DisplayConfig disp = display_get_config();
    printk("streaming %dx%d\n", cfg.width, cfg.height);

    img_kernel_init();
    CameraFrame frame;
    Image* img = img_kmalloc();
    uint32_t t = 0;
    while (1) {
        if (camera_capture_frame(&frame)) {
            // printk("t: %d\n", sys_timer_get_usec() - t);
            // t = sys_timer_get_usec();

            img_init_frame(img, &frame);

            img_bw_norm_gray_world_wb(img, 1023, 64);
            img_debayer_fast(img, display_get_buffer(), false);

            display_swap();
        }
    }

    camera_stop();
}
