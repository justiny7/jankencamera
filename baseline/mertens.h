#pragma once

#include "image.h"

#include <array>

class MertensExposure {
public:
    MertensExposure(std::vector<Image> imgs);

    Image fuse();

// private:
    int width_;
    int height_;
    int depth_;
    int img_size_;
    int num_imgs_;
    int num_levels_;
    int pmax_;

    std::vector<Image> imgs_;

    static Image convolve_laplacian_abs(const Image& in);
    static Image convolve_gaussian(const Image& in, float mul = 1.f);
    static Image downsample(const Image& in);
    static Image upsample(const Image& in, int width, int height);

    static Image compute_saturation_weight(const Image& in);
    static Image compute_contrast_weight(const Image& in);
    static Image compute_exposedness_weight(const Image& in);
    static Image compute_weight_map(const Image& in);

    void normalize_weight_maps(std::vector<Image>& weight_maps);
    std::vector<Image> build_gaussian_pyramid(const Image& in);
    std::vector<Image> build_laplacian_pyramid(const Image& in);
    std::vector<Image> blend_pyramids(
        const std::vector<std::vector<Image>>& laplacians,
        const std::vector<std::vector<Image>>& weights);
    Image collapse_pyramid(const std::vector<Image>&pyramid);

    static constexpr std::array<float, 5> gaussian_kernel_ = {
        0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f
    };
    static constexpr float F_EPS = 1e-6f;

};
