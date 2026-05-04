#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CAM_FMT_BAYER_8,
    CAM_FMT_BAYER_10, // 16-bit pixels
} CameraFormat;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride; // bytes per row
    CameraFormat format;
} CameraConfig;

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t sequence;
} CameraFrame;

bool camera_init();
bool camera_set_format(uint32_t width, uint32_t height, CameraFormat fmt);
CameraConfig camera_get_config();

bool camera_start();
void camera_stop();

bool camera_capture_frame(CameraFrame* frame);
void camera_release_frame(CameraFrame* frame);

bool camera_set_exposure(uint32_t value);
bool camera_set_analog_gain(uint32_t value);
bool camera_set_digital_gain(uint32_t value);
bool camera_set_vflip(bool enable);
bool camera_set_hflip(bool enable);

#endif
