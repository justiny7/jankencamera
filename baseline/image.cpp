#include "image.h"

#include <algorithm>
#include <fstream>
#include <cassert>

Image::Image(int width, int height, int depth, PixelFormat fmt) :
        width_(width), height_(height), size_(width * height),
        depth_(depth), fmt_(fmt),
        data_(width * height * static_cast<int>(fmt)) { }

Image::Image(int width, int height, int depth, std::vector<float> data) :
        width_(width), height_(height), size_(width * height),
        depth_(depth), data_(std::move(data)) {

    if (data_.size() == static_cast<size_t>(size_)) {
        fmt_ = GRAY;
    } else {
        assert(data_.size() == static_cast<size_t>(size_) * 3);
        fmt_ = RGB;
    }
}

// RGGB bayer
int Image::get_bayer_channel(int i) const {
    assert(fmt_ == GRAY);

    if (i < 0 || i >= size_) return -1;
    int y = i / width_, x = i % width_;
    return (y & 1) * 2 + (x & 1);
}

void Image::black_white_norm(int white_level, int black_level) {
    assert(fmt_ == GRAY);
    for (float& i : data_) {
        i = std::clamp(i - black_level, 0.f, (float) white_level - black_level);
        i *= 1.f * pixel_max(depth_) / (white_level - black_level);
    }
}

void Image::gray_world_wb(bool wb_intensity,
        float wb_intensity_threshold) {
    assert(fmt_ == GRAY);
    std::vector<float> avg(4);
    std::vector<int> cnt(4);
    for (int i = 0; i < size_; i++) {
        if (!wb_intensity || data_[i] < pixel_max(depth_) * wb_intensity_threshold) {
            int c = get_bayer_channel(i);
            avg[c] += data_[i];
            cnt[c]++;
        }
    }

    for (int i = 0; i < 4; i++) {
        avg[i] /= cnt[i];
    }

    double g_avg = (avg[1] + avg[2]) / 2;
    for (int i = 0; i < size_; i++) {
        int c = get_bayer_channel(i);
        double gain = g_avg / avg[c];
        data_[i] = (int) std::clamp(gain * data_[i], 0.0, (double) pixel_max(depth_));
    }
}

void Image::debayer() {
    assert(fmt_ == GRAY);
    std::vector<float> new_data(size_ * 3);

    // assumes RGGB
    for (int y = 0; y < height_; y++) {
        for (int x = 0; x < width_; x++) {
            int i = y * width_ + x;
            int s = width_;

            // border reflection
            int xl = (x == 0 ? 1 : -1);
            int xr = (x == width_ - 1 ? -1 : 1);
            int yl = (y == 0 ? s : -s);
            int yr = (y == height_ - 1 ? -s : s);

            float p = data_[i];
            float pl = data_[i + xl];
            float pul = data_[i + yl + xl];
            float pu = data_[i + yl];
            float pur = data_[i + yl + xr];
            float pr = data_[i + xr];
            float pdr = data_[i + yr + xr];
            float pd = data_[i + yr];
            float pdl = data_[i + yr + xl];

            int r, g, b;
            if (y & 1) {
                if (x & 1) { // b
                    r = (pul + pur + pdl + pdr) / 4;
                    g = (pl + pu + pr + pd) / 4;
                    b = p;
                } else { // g2
                    r = (pu + pd) / 2;
                    g = p;
                    b = (pl + pr) / 2;
                }
            } else {
                if (x & 1) { // g1
                    r = (pl + pr) / 2;
                    g = p;
                    b = (pu + pd) / 2;
                } else { // r
                    r = p;
                    g = (pl + pu + pr + pd) / 4;
                    b = (pul + pur + pdl + pdr) / 4;
                }
            }

            new_data[i * 3] = r;
            new_data[i * 3 + 1] = g;
            new_data[i * 3 + 2] = b;
        }
    }

    data_.swap(new_data);
    fmt_ = RGB;
}

void Image::write_ppm(std::string filename) const {
    std::ofstream fout(filename + ".ppm");
    if (fmt_ == GRAY) {
        fout << "P2\n" << width_ << " " << height_ << '\n';
    } else {
        fout << "P3\n" << width_ << " " << height_ << '\n';
    }

    fout << pixel_max(depth_) << '\n';
    for (float f : data_) {
        fout << static_cast<int>(f) << " ";
    }

    fout.close();
}

void Image::dump_data(std::string filename) const {
    std::ofstream fout(filename + ".txt");

    if (fmt_ == GRAY) {
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                fout << std::fixed << std::setprecision(4) <<
                    get_data_at(y, x, 0) << " \n"[x == width_ - 1];
            }
        }
    } else {
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                fout << std::fixed << std::setprecision(4) << "( " <<
                    get_data_at(y, x, 0) << ", " <<
                    get_data_at(y, x, 1) << ", " <<
                    get_data_at(y, x, 2) << ")" << " \n"[x == width_ - 1];
            }
        }
    }
    fout.close();
}

std::vector<Image::Pixel> Image::get_pixels() const {
    std::vector<Image::Pixel> pixels(size_);
    if (fmt_ == GRAY) {
        for (int i = 0; i < size_; i++) {
            pixels[i] = Image::Pixel {
                static_cast<int>(data_[i]),
                static_cast<int>(data_[i]),
                static_cast<int>(data_[i])
            };
        }
    } else {
        for (int i = 0; i < size_; i++) {
            pixels[i] = Image::Pixel {
                static_cast<int>(data_[i * 3]),
                static_cast<int>(data_[i * 3 + 1]),
                static_cast<int>(data_[i * 3 + 2])
            };
        }
    }
    return pixels;
}

float Image::get_r_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3);
}
float Image::get_g_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3 + 1);
}
float Image::get_b_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3 + 2);
}
float Image::get_r_norm_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3) / pixel_max(depth_);
}
float Image::get_g_norm_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3 + 1) / pixel_max(depth_);
}
float Image::get_b_norm_at(int idx) const {
    assert(fmt_ == RGB);
    return data_.at(idx * 3 + 2) / pixel_max(depth_);
}

bool Image::in_bounds(int y, int x, int c) const {
    return (y >= 0 && y < height_ &&
            x >= 0 && x < width_ &&
            c >= 0 && c < get_channels());
}
int Image::get_idx(int y, int x, int c) const {
    assert(in_bounds(y, x, c));
    return (y * width_ + x) * get_channels() + c;
}
bool Image::same_shape(const Image& a, const Image& b) {
    return a.get_width() == b.get_width() &&
        a.get_height() == b.get_height() &&
        a.get_channels() == b.get_channels();
}
Image Image::add(const Image& a, const Image& b) {
    assert(same_shape(a, b));

    int w = a.get_width();
    int h = a.get_height();
    int c = a.get_channels();
    Image out(w, h, a.get_depth(), a.get_format());

    int sz = w * h * c;
    for (int i = 0; i < sz; i++) {
        out.set_data_at(i, a.get_data_at(i) + b.get_data_at(i));
    }
    return out;
}
Image Image::sub(const Image& a, const Image& b) {
    assert(same_shape(a, b));

    int w = a.get_width();
    int h = a.get_height();
    int c = a.get_channels();
    Image out(w, h, a.get_depth(), a.get_format());

    int sz = w * h * c;
    for (int i = 0; i < sz; i++) {
        out.set_data_at(i, a.get_data_at(i) - b.get_data_at(i));
    }
    return out;
}
Image Image::mul(const Image& a, const Image& b) {
    assert(same_shape(a, b));

    int w = a.get_width();
    int h = a.get_height();
    int c = a.get_channels();
    Image out(w, h, a.get_depth(), a.get_format());

    int sz = w * h * c;
    for (int i = 0; i < sz; i++) {
        out.set_data_at(i, a.get_data_at(i) * b.get_data_at(i));
    }
    return out;
}

Image Image::image_like() const {
    return Image(width_, height_, depth_, fmt_);
}
Image Image::gray_like() const {
    return Image(width_, height_, depth_, GRAY);
}
Image Image::rgb_like() const {
    return Image(width_, height_, depth_, RGB);
}

float Image::get_luma(float r, float g, float b) {
    return r * luma_r + g * luma_g + b * luma_b;
}
Image Image::to_grayscale() const {
    assert(fmt_ == RGB);

    Image out(width_, height_, depth_, GRAY);
    for (int i = 0; i < size_; i++) {
        out.set_data_at(i,
                get_luma(get_r_at(i), get_g_at(i), get_b_at(i)));
    }

    return out;
}
Image Image::to_rgb() const {
    assert(fmt_ == GRAY);

    Image out(width_, height_, depth_, RGB);
    for (int i = 0; i < size_; i++) {
        out.set_data_at(i * 3, data_[i]);
        out.set_data_at(i * 3 + 1, data_[i]);
        out.set_data_at(i * 3 + 2, data_[i]);
    }

    return out;
}
