#include "image_pipeline.h"

#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>

void ImagePipeline::convert_fmt(PixelFormat fmt) {
    double conv = 1.0 * (fmt_max(fmt) + 1) / (fmt_max(fmt_) + 1);
    for (int &i : bayer_) i *= conv;
}
int ImagePipeline::get_channel(int i) const {
    if (i < 0 || i >= img_size_) return -1;
    int y = i / width_, x = i % width_;
    return (y & 1) * 2 + (x & 1);
}

void ImagePipeline::black_white_norm(int white_level, int black_level) {
    for (int &i : bayer_) {
        double norm = std::clamp(i - black_level, 0, white_level - black_level);
        norm *= 1.0 * fmt_max(fmt_) / (white_level - black_level);
        i = (int) norm;
    }
}
void ImagePipeline::gray_world_wb(bool wb_intensity,
        double wb_intensity_threshold) {
    std::vector<double> avg(4);
    std::vector<int> cnt(4);
    for (int i = 0; i < img_size_; i++) {
        if (!wb_intensity || bayer_[i] < fmt_max(fmt_) * wb_intensity_threshold) {
            int c = get_channel(i);
            avg[c] += bayer_[i];
            cnt[c]++;
        }
    }

    for (int i = 0; i < 4; i++) {
        avg[i] /= cnt[i];
    }

    double g_avg = (avg[1] + avg[2]) / 2;
    for (int i = 0; i < img_size_; i++) {
        int c = get_channel(i);
        double gain = g_avg / avg[c];
        bayer_[i] = (int) std::clamp(gain * bayer_[i], 0.0, (double) fmt_max(fmt_));
    }
}
void ImagePipeline::debayer() {
    img_ = std::vector<Pixel>(img_size_);

    // assumes RGGB
    for (int y = 0; y < height_; y++) {
        for (int x = 0; x < width_; x++) {
            if (y == 0 || x == 0) {
                continue;
            }

            std::vector<int>& p = bayer_;
            int i = y * width_ + x;
            int s = width_;

            int r, g, b;
            if (y & 1) {
                if (x & 1) { // b
                    r = (p[i - s - 1] + p[i - s + 1] + p[i + s - 1] + p[i + s + 1]) / 4;
                    g = (p[i - 1] + p[i + 1] + p[i - s] + p[i + s]) / 4;
                    b = p[i];
                } else { // g2
                    r = (p[i - s] + p[i + s]) / 2;
                    g = p[i];
                    b = (p[i - 1] + p[i + 1]) / 2;
                }
            } else {
                if (x & 1) { // g1
                    r = (p[i - 1] + p[i + 1]) / 2;
                    g = p[i];
                    b = (p[i - s] + p[i + s]) / 2;
                } else { // r
                    r = p[i];
                    g = (p[i - 1] + p[i + 1] + p[i - s] + p[i + s]) / 4;
                    b = (p[i - s - 1] + p[i - s + 1] + p[i + s - 1] + p[i + s + 1]) / 4;
                }
            }

            img_[i] = Pixel { r, g, b };
        }
    }
}
void ImagePipeline::write_ppm(std::string filename) {
    std::ofstream fout(filename + ".ppm");
    fout << "P3\n" << width_ << " " << height_ << "\n";
    fout << fmt_max(fmt_) << '\n';
    for (const Pixel& p : img_) {
        fout << p.r << " " << p.g << " " << p.b << '\n';
    }
    fout.close();
}

