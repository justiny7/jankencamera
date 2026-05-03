#include "camera.h"
#include "unicam.h"
#include "imx219.h"
#include "lib.h"
#include "mailbox_interface.h"

#define MAX_FRAME_SIZE (1920 * 1080 * 2)

// three buffers for triple buffering
static uint8_t __attribute__((aligned(32))) g_frame_buf0[MAX_FRAME_SIZE];
static uint8_t __attribute__((aligned(32))) g_frame_buf1[MAX_FRAME_SIZE];
static uint8_t __attribute__((aligned(32))) g_frame_buf2[MAX_FRAME_SIZE];
static CameraConfig g_config;
static bool g_streaming = false;
static uint32_t g_sequence = 0;

bool camera_init() {
    if (!unicam_init()) {
        printk("camera: unicam init failed\n");
        return false;
    }

    if (!imx219_init()) {
        printk("camera: imx219 init failed\n");
        unicam_deinit();
        return false;
    }

    printk("camera: initialized\n");
    return true;
}

bool camera_set_format(uint32_t width, uint32_t height, CameraFormat fmt) {
    if (g_streaming) return false;

    IMX219Mode mode;
    if (width <= 640 && height <= 480)
        mode = IMX219_MODE_640x480;
    else
        mode = IMX219_MODE_1920x1080;

    uint8_t depth = (fmt == CAM_FMT_BAYER_10) ? 10 : 8;

    if (!imx219_set_mode(mode, depth)) {
        printk("camera: set mode failed\n");
        return false;
    }

    IMX219ModeInfo info = imx219_get_mode_info();
    g_config.width = info.width;
    g_config.height = info.height;
    g_config.format = fmt;
    g_config.stride = (info.width * (depth == 10 ? 2 : 1) + 31) & ~31;

    uint32_t frame_size = g_config.stride * g_config.height;
    
    UniCamConfig ucfg = {
        .width = g_config.width,
        .height = g_config.height,
        .stride = g_config.stride,
        .depth = depth,
        .buffer = g_frame_buf0,
        .buffer_size = frame_size,
    };

    if (!unicam_configure(&ucfg)) {
        printk("camera: unicam configure failed\n");
        return false;
    }
    
    // register triple buffers
    unicam_set_triple_buffer(g_frame_buf0, g_frame_buf1, g_frame_buf2, frame_size);

    printk("camera: format %dx%d stride=%d\n",
           g_config.width, g_config.height, g_config.stride);
    return true;
}

CameraConfig camera_get_config() {
    return g_config;
}

bool camera_start() {
    if (g_streaming) return true;

    if (!unicam_start()) {
        printk("camera: unicam start failed\n");
        return false;
    }

    if (!imx219_start_streaming()) {
        printk("camera: sensor start failed\n");
        unicam_stop();
        return false;
    }

    g_streaming = true;
    g_sequence = 0;
    printk("camera: streaming started\n");
    return true;
}

void camera_stop() {
    if (!g_streaming) return;
    imx219_stop_streaming();
    unicam_stop();
    g_streaming = false;
    printk("camera: streaming stopped\n");
}

bool camera_capture_frame(CameraFrame* frame) {
    if (!g_streaming || !frame) return false;

    if (!unicam_wait_frame()) return false;

    // get buffer that just completed
    uint8_t* ready_buf = unicam_get_ready_buffer();
    if (!ready_buf) return false;

    frame->data = ready_buf;
    frame->size = g_config.stride * g_config.height;
    frame->sequence = g_sequence++;
    return true;
}

void camera_release_frame(CameraFrame* frame) {
    (void) frame;
    unicam_release_buffer();
}

bool camera_set_exposure(uint32_t value) {
    return imx219_set_exposure(value);
}

bool camera_set_gain(uint32_t value) {
    return imx219_set_gain(value);
}

bool camera_set_vflip(bool enable) {
    return imx219_set_vflip(enable);
}

bool camera_set_hflip(bool enable) {
    return imx219_set_hflip(enable);
}
