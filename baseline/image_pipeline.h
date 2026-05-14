#pragma once

#include <string>
#include <vector>
#include <cassert>

class ImagePipeline {
public:
    enum PixelFormat { RGB8 = 8, RGB10 = 10 };
    struct Pixel { int r = 0, g = 0, b = 0; };

    ImagePipeline(int width, int height, std::vector<int> bayer, PixelFormat fmt) :
        width_(width),
        height_(height),
        img_size_(width * height),
        fmt_(fmt),
        bayer_(std::move(bayer)) {
        assert(bayer_.size() == (size_t) img_size_);
    }

    void convert_fmt(PixelFormat fmt);
    void black_white_norm(int white_level, int black_level);
    void gray_world_wb(bool wb_intensity = false,
            double wb_intensity_threshold = 0.95);
    void debayer();
    void write_ppm(std::string filename);

private:
    int width_;
    int height_;
    int img_size_;

    PixelFormat fmt_;
    std::vector<int> bayer_;
    std::vector<Pixel> img_;

    int get_channel(int i) const;
    static int fmt_max(PixelFormat fmt) {
        return ((1 << fmt) - 1);
    }
};


