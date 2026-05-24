#include "mertens.h"

#include <cmath>
#include <cassert>

#include <fstream>
#include <iostream>

#define DEBUG 0

MertensExposure::MertensExposure(std::vector<Image> imgs) :
    imgs_(std::move(imgs)) {
    assert(!imgs_.empty());

    num_imgs_ = imgs_.size();
    width_ = imgs_[0].get_width();
    height_ = imgs_[0].get_height();
    depth_ = imgs_[0].get_depth();
    img_size_ = imgs_[0].get_size();
    pmax_ = imgs_[0].get_pmax();

    // num_levels_ = static_cast<int>(std::log2(std::min(width_, height_))) - 3;
    num_levels_ = static_cast<int>(std::log2(std::min(width_, height_))) - 2;
    if (num_levels_ < 1) num_levels_ = 1;

    // assert even width/height
    assert(width_ % 2 == 0);
    assert(height_ % 2 == 0);
    assert(width_ >= 5 && height_ >= 5); // make sure biggest filter won't error

    for (const Image& img : imgs_) {
        assert(img.get_format() == Image::RGB); // all RGB + same shape
        assert(Image::same_shape(img, imgs_[0]));
    }
}

Image MertensExposure::fuse() {
    for (Image& img : imgs_) {
        for (int i = 0; i < img_size_ * 3; i++) {
            img.set_data_at(i, img.get_data_at(i) / pmax_);
        }
    }

    std::vector<Image> weight_maps;
    for (int i = 0; i < num_imgs_; i++) {
        weight_maps.push_back(compute_weight_map(imgs_[i]));
    }

    normalize_weight_maps(weight_maps);

    std::vector<std::vector<Image>> laplacians;  // RGB
    std::vector<std::vector<Image>> weights;     // grayscale
    laplacians.reserve(num_imgs_);
    weights.reserve(num_imgs_);
    for (int i = 0; i < num_imgs_; i++) {
        laplacians.push_back(build_laplacian_pyramid(imgs_[i]));
        weights.push_back(build_gaussian_pyramid(weight_maps[i]));

#if DEBUG
        for (int l = 0; l < num_levels_; l++) {
            laplacians[i][l].write_ppm("DEBUG_laplacian_" + std::to_string(i) +
                    "_" + std::to_string(l));
            weights[i][l].dump_data("DEBUG_weight_" + std::to_string(i) +
                    "_" + std::to_string(l));
        }
#endif
    }

    std::vector<Image> blended = blend_pyramids(laplacians, weights);

    Image out = collapse_pyramid(blended);
    for (int i = 0; i < img_size_ * 3; i++) {
        out.set_data_at(i, std::clamp(out.get_data_at(i) * pmax_,
                    0.f, static_cast<float>(pmax_)));
    }
    
    return out;
}

Image MertensExposure::convolve_laplacian_abs(const Image& in) {
    int w = in.get_width();
    int h = in.get_height();
    int channels = in.get_channels();

    Image out = in.image_like();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < channels; c++) {
                int xl = (x == 0 ? 1 : x - 1);
                int xr = (x == w - 1 ? w - 2 : x + 1);
                int yl = (y == 0 ? 1 : y - 1);
                int yr = (y == h - 1 ? h - 2 : y + 1);
                float sum = in.get_data_at(y, xl, c) +
                    in.get_data_at(y, xr, c) +
                    in.get_data_at(yl, x, c) +
                    in.get_data_at(yr, x, c) -
                    in.get_data_at(y, x, c) * 4.f;

                out.set_data_at(y, x, c, std::abs(sum));
            }
        }
    }

    return out;
}
Image MertensExposure::convolve_gaussian(const Image& in, float mul) {
    int n = gaussian_kernel_.size();
    int r = n / 2;

    int w = in.get_width();
    int h = in.get_height();
    int channels = in.get_channels();

    Image temp = in.image_like();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < channels; c++) {
                float sum = 0.f;

                for (int k = -r; k <= r; k++) {
                    int px = x + k;
                    if (px < 0) px = -px;
                    if (px >= w) px = 2 * (w - 1) - px;
                    sum += in.get_data_at(y, px, c) * gaussian_kernel_[k + r];
                }

                temp.set_data_at(y, x, c, sum * mul);
            }
        }
    }

    Image out = in.image_like();
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < channels; c++) {
                float sum = 0.f;

                for (int k = -r; k <= r; k++) {
                    int py = y + k;
                    if (py < 0) py = -py;
                    if (py >= h) py = 2 * (h - 1) - py;
                    sum += temp.get_data_at(py, x, c) * gaussian_kernel_[k + r];
                }

                out.set_data_at(y, x, c, sum * mul);
            }
        }
    }

    return out;
}
Image MertensExposure::downsample(const Image& in) {
    Image temp = convolve_gaussian(in);

    int nw = in.get_width() / 2;
    int nh = in.get_height() / 2;

    Image out(nw, nh, in.get_depth(), in.get_format());
    for (int y = 0; y < nh; y++) {
        for (int x = 0; x < nw; x++) {
            for (int c = 0; c < in.get_channels(); c++) {
                out.set_data_at(y, x, c, temp.get_data_at(y * 2, x * 2, c));
            }
        }
    }

    return out;
}
Image MertensExposure::upsample(const Image& in, int width, int height) {
    Image temp(width, height, in.get_depth(), in.get_format());

    for (int y = 0; y < in.get_height(); y++) {
        for (int x = 0; x < in.get_width(); x++) {
            for (int c = 0; c < in.get_channels(); c++) {
                temp.set_data_at(y * 2, x * 2, c, in.get_data_at(y, x, c));
            }
        }
    }

    return convolve_gaussian(temp, 2.f);
}

Image MertensExposure::compute_saturation_weight(const Image& in) {
    assert(in.get_format() == Image::RGB);

    const std::vector<float>& data = in.get_data();

    Image out = in.gray_like();
    for (int i = 0; i < in.get_size(); i++) {
        float avg = (data[i * 3] + data[i * 3 + 1] + data[i * 3 + 2]) / 3.f;
        float dr = data[i * 3] - avg;
        float dg = data[i * 3 + 1] - avg;
        float db = data[i * 3 + 2] - avg;
        float std = std::sqrt((dr * dr + dg * dg + db * db) / 3.f);
        out.set_data_at(i, std);

        /*
        // Chroma approximation
        float r = data[i * 3];
        float g = data[i * 3 + 1];
        float b = data[i * 3 + 2];
        out.set_data_at(i, std::max({r, g, b}) - std::min({r, g, b}));
        */
    }

    return out;
}
Image MertensExposure::compute_contrast_weight(const Image& in) {
    assert(in.get_format() == Image::RGB);
    return convolve_laplacian_abs(in.to_grayscale());
}
Image MertensExposure::compute_exposedness_weight(const Image& in) {
    assert(in.get_format() == Image::RGB);

    const float sigma = 0.2f;
    const float exp_mul = -1.0f / (2.0f * sigma * sigma);
    const std::vector<float>& data = in.get_data();

    Image out = in.gray_like();
    for (int i = 0; i < in.get_size(); i++) {
        float exposedness = 1.f;
        for (int c = 0; c < 3; c++) {
            float val = data[i * 3 + c] - 0.5f;
            exposedness *= std::exp(val * val * exp_mul);
        }

        out.set_data_at(i, exposedness);
    }

    return out;
}
Image MertensExposure::compute_weight_map(const Image& in) {
    Image saturation = compute_saturation_weight(in);
    Image contrast = compute_contrast_weight(in);
    Image exposedness = compute_exposedness_weight(in);

    Image out = in.gray_like();
    for (int i = 0; i < in.get_size(); i++) {
        out.set_data_at(i,
                saturation.get_data_at(i) *
                contrast.get_data_at(i) *
                exposedness.get_data_at(i));
    }
    return out;
}
void MertensExposure::normalize_weight_maps(std::vector<Image>& weight_maps) {
    for (int i = 0; i < img_size_; i++) {
        float sum = 0.f;
        for (int j = 0; j < num_imgs_; j++) {
            sum += weight_maps[j].get_data_at(i);
        }
        if (sum > F_EPS) {
            for (int j = 0; j < num_imgs_; j++) {
                weight_maps[j].set_data_at(i, weight_maps[j].get_data_at(i) / sum);
            }
        } else {
            for (int j = 0; j < num_imgs_; j++) {
                weight_maps[j].set_data_at(i, 1.f / num_imgs_);
            }
        }
    }
}
std::vector<Image> MertensExposure::build_gaussian_pyramid(const Image& in) {
    std::vector<Image> out;
    out.reserve(num_levels_);

    out.push_back(in);
    for (int i = 1; i < num_levels_; i++) {
        out.push_back(downsample(out.back()));
    }

    return out;
}
std::vector<Image> MertensExposure::build_laplacian_pyramid(const Image& in) {
    std::vector<Image> gauss = build_gaussian_pyramid(in);

    std::vector<Image> out;
    out.reserve(num_levels_);

    for (int i = 0; i < num_levels_ - 1; i++) {
        Image upsampled = upsample(gauss[i + 1],
                gauss[i].get_width(), gauss[i].get_height());
        out.push_back(Image::sub(gauss[i], upsampled));
    }
    out.push_back(gauss.back());

    return out;
}
std::vector<Image> MertensExposure::blend_pyramids(
        const std::vector<std::vector<Image>>& laplacians,
        const std::vector<std::vector<Image>>& weights) {

    assert(laplacians.size() == static_cast<size_t>(num_imgs_));
    assert(weights.size() == static_cast<size_t>(num_imgs_));
    assert(laplacians[0].size() == static_cast<size_t>(num_levels_));
    assert(weights[0].size() == static_cast<size_t>(num_levels_));

    std::vector<Image> out;
    out.reserve(num_levels_);

    for (int i = 0; i < num_levels_; i++) {
        Image sum = Image::mul(laplacians[0][i], weights[0][i].to_rgb());
        for (int j = 1; j < num_imgs_; j++) {
            sum = Image::add(sum, Image::mul(laplacians[j][i], weights[j][i].to_rgb()));
        }

        out.push_back(std::move(sum));
    }

    return out;
}
Image MertensExposure::collapse_pyramid(const std::vector<Image>&pyramid) {
    assert(pyramid.size() == static_cast<size_t>(num_levels_));

    Image out = pyramid.back();
    for (int i = num_levels_ - 2; i >= 0; i--) {
        Image up = upsample(out,
                pyramid[i].get_width(), pyramid[i].get_height());
        out = Image::add(up, pyramid[i]);
    }

    return out;
}
