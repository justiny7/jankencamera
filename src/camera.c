#include "camera.h"
#include "unicam.h"
#include "imx219.h"
#include "lib.h"
#include "mailbox_interface.h"

#define MAX_FRAME_SIZE  (1920 * 1080 * 2)
#define MHZ             1000000

// three buffers for triple buffering
static uint8_t __attribute__((aligned(32))) g_frame_buf0[MAX_FRAME_SIZE];
static uint8_t __attribute__((aligned(32))) g_frame_buf1[MAX_FRAME_SIZE];
static uint8_t __attribute__((aligned(32))) g_frame_buf2[MAX_FRAME_SIZE];
static uint8_t __attribute__((aligned(32))) g_dummy_buf[MAX_FRAME_SIZE];

static CameraConfig g_config;
static bool g_streaming = false;
static uint32_t g_sequence = 0;

static uint32_t us_to_lines(uint32_t us) {
    return (uint32_t) (us * IMX219_PIXEL_RATE_MHZ / imx219_get_HTS() + 0.5f);
}
static uint32_t lines_to_us(uint32_t lines) {
    return (uint32_t) (lines * imx219_get_HTS() / IMX219_PIXEL_RATE_MHZ + 0.5f);
}

// https://android.googlesource.com/kernel/bcm/+/android-bcm-tetra-3.10-lollipop-wear-release/drivers/media/video/imx219.c
static uint8_t ana_gain_to_reg(float gain) {
    if (gain < 1.0) gain = 1.0;
    return (uint8_t) (256.f - 256.f / gain + 0.5f);
}
static float ana_reg_to_gain(uint8_t value) {
    return 256.f / (256 - value);
}
// float <-> 8.8FP
static uint16_t dig_gain_to_reg(float gain) {
    if (gain < 1.0) gain = 1.0;
    return (uint16_t) (gain * 256.f + 0.5f);
}
static float dig_reg_to_gain(uint16_t value) {
    return value / 256.f;
}

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
    if (width <= 640 && height <= 480) {
        mode = IMX219_MODE_640x480;
    } else {
        mode = IMX219_MODE_1920x1080;
    }

    uint8_t depth = (fmt == CAM_FMT_BAYER_10) ? 10 : 8;

    if (!imx219_set_mode(mode, depth)) {
        printk("camera: set mode failed\n");
        return false;
    }

    IMX219ModeInfo info = imx219_get_mode_info();
    g_config.width = info.width;
    g_config.height = info.height;
    g_config.format = fmt;
    // depth = 10 -> uint16 = 2 bytes per row, rounded to nearest 32
    g_config.stride = (info.width * (depth == 10 ? 2 : 1) + 31) & ~31;

    uint32_t frame_size = g_config.stride * g_config.height;
    
    UnicamConfig ucfg = {
        .width = g_config.width,
        .height = g_config.height,
        .stride = g_config.stride,
        .depth = depth,
        .buffers = { g_frame_buf0, g_frame_buf1, g_frame_buf2 },
        .dummy_buffer = g_dummy_buf,
        .buffer_size = frame_size,
    };

    if (!unicam_configure(&ucfg)) {
        printk("camera: unicam configure failed\n");
        return false;
    }

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
bool camera_capture_frame_shot(CameraFrame* frame, CameraShot shot) {
    if (!g_streaming || !frame) return false;

    if (shot.exposure && !camera_set_exposure(shot.exposure)) return false;

    // auto set gain or control digital/analog separately
    if (shot.gain != 0) {
        if (!camera_set_gain(shot.gain))  return false;
    } else {
        if (shot.ana_gain != 0 && !camera_set_analog_gain(shot.ana_gain)) return false;
        if (shot.dig_gain != 0 && !camera_set_digital_gain(shot.dig_gain)) return false;
    }

    // discard 4 frames, get 5th
    for (int i = 0; i < 4; i++) {
        if (!unicam_wait_frame()) {
            return false;
        }
    }

    // get buffer that just completed
    uint8_t* ready_buf = unicam_get_ready_buffer();
    if (!ready_buf) return false;

    frame->data = ready_buf;
    frame->size = g_config.stride * g_config.height;
    frame->sequence = g_sequence++;

    float ana_gain = ana_reg_to_gain(imx219_get_analog_gain());
    float dig_gain = dig_reg_to_gain(imx219_get_digital_gain());
    frame->shot = (CameraShot) {
        .exposure = lines_to_us(imx219_get_exposure()),
        .gain = ana_gain * dig_gain,
        .ana_gain = ana_gain,
        .dig_gain = dig_gain,
    };
    return true;
}

void camera_release_frame(CameraFrame* frame) {
    (void) frame;
    unicam_release_buffer();
}


bool camera_set_exposure(uint32_t us) {
    uint32_t value = us_to_lines(us);
    // printk("us: %d -> lines: %d\n", us, value);
    return imx219_set_exposure(value);
}

bool camera_set_gain(float gain) {
    float ana_gain = gain, dig_gain = 1.0;
    if (gain > 8.0) { // max analog gain
        ana_gain = 8.0;
        dig_gain = gain / 8.0;
    }

    printk("gain: %f\tana gain: %f\tdig gain: %f\n",
            gain, ana_gain, dig_gain);
    return camera_set_analog_gain(ana_gain) &&
        camera_set_digital_gain(dig_gain);
}
bool camera_set_analog_gain(float ana_gain) {
    uint32_t value = ana_gain_to_reg(ana_gain);
    // printk("gain: %f -> val: %d\n", gain, value);
    return imx219_set_gain(value);
}
bool camera_set_digital_gain(float dig_gain) {
    uint32_t value = dig_gain_to_reg(dig_gain);
    return imx219_set_digital_gain(value);
}
bool camera_set_vflip(bool enable) {
    return imx219_set_vflip(enable);
}
bool camera_set_hflip(bool enable) {
    return imx219_set_hflip(enable);
}
