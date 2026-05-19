#pragma once

#include <vector>
#include <string>

class Image {
public:
    enum PixelFormat { GRAY = 1, RGB = 3 };
    struct Pixel { int r = 0, g = 0, b = 0; };

    Image() = default;
    Image(int width, int height, int depth, PixelFormat fmt);
    Image(int width, int height, int depth, std::vector<float> data);

    void black_white_norm(int white_level, int black_level);
    void gray_world_wb(bool wb_intensity, float wb_intensity_threshold);
    void debayer();
    void write_ppm(std::string filename) const;

    // helpers
    bool in_bounds(int y, int x, int c)const ;
    int get_idx(int y, int x, int c) const;
    void dump_data(std::string filename) const;
    Image image_like() const;
    Image gray_like() const;
    Image rgb_like() const;
    Image to_grayscale() const;
    Image to_rgb() const;

    // getters
    int get_width() const { return width_; }
    int get_height() const { return height_; }
    int get_size() const { return size_; }
    int get_depth() const { return depth_; }
    int get_channels() const { return static_cast<int>(fmt_); }
    int get_pmax() const { return pixel_max(depth_); }
    float get_data_at(int idx) const { return data_.at(idx); }
    float get_data_at(int y, int x, int c) const { return data_.at(get_idx(y, x, c)); }
    PixelFormat get_format() const { return fmt_; }
    const std::vector<float>& get_data() const { return data_; }
    std::vector<Pixel> get_pixels() const;

    float get_r_at(int idx) const;
    float get_g_at(int idx) const;
    float get_b_at(int idx) const;
    float get_r_norm_at(int idx) const;
    float get_g_norm_at(int idx) const;
    float get_b_norm_at(int idx) const;

    // setters
    void fill_data(float val) { data_.assign(size_, val); }
    void set_data_at(int idx, float val) { data_.at(idx) = val; }
    void set_data_at(int y, int x, int c, float val) { data_.at(get_idx(y, x, c)) = val; }

    static bool same_shape(const Image& a, const Image& b);
    static Image add(const Image& a, const Image& b);
    static Image sub(const Image& a, const Image& b);
    static Image mul(const Image& a, const Image& b);

    static float get_luma(float r, float g, float b);

private:
    int width_ = 0;
    int height_ = 0;
    int size_ = 0;
    int depth_ = 0;
    PixelFormat fmt_ = GRAY;
    std::vector<float> data_;

    int get_bayer_channel(int i) const;
    static int pixel_max(int depth) { return (1 << depth) - 1; }

    static constexpr float luma_r = 0.299f;
    static constexpr float luma_g = 0.587f;
    static constexpr float luma_b = 0.114f;
};

