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
    CameraConfig cfg;

    uint32_t exposure;  // microseconds
    float gain;         // total gain multiplier
    float ana_gain;     // analog gain
    float dig_gain;     // digital gain
    uint32_t white_bal;
} CameraShot;

typedef struct {
    uint8_t* data;
    uint32_t size;
    uint32_t sequence;

    CameraShot shot;
} CameraFrame;

bool camera_init();
bool camera_set_format(uint32_t width, uint32_t height, CameraFormat fmt);
CameraConfig camera_get_config();

bool camera_start();
void camera_stop();

bool camera_capture_frame(CameraFrame* frame);
bool camera_capture_frame_shot(CameraFrame* frame, CameraShot shot);
void camera_release_frame(CameraFrame* frame);

bool camera_set_exposure(uint32_t us);
bool camera_set_gain(float gain);
bool camera_set_analog_gain(float ana_gain);
bool camera_set_digital_gain(float dig_gain);
bool camera_set_vflip(bool enable);
bool camera_set_hflip(bool enable);

#endif
