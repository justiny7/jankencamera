#include "image.h"
#include "mertens.h"

#include <iostream>
#include <fstream>

#define TEST 0

#if TEST
const std::string input_fname = "test.txt";
const std::string output_prefix = "test_output_";
const int num_imgs = 3;
const int width = 1200;
const int height = 800;
#else
const std::string input_fname = "input.txt";
const std::string output_prefix = "output_";
const int num_imgs = 5;
const int width = 640;
const int height = 480;
#endif

const int img_size = width * height;

// black/white levels on 10-bit pixels
const int black_level = 64;
const int white_level = 1023;

const double wb_intensity_threshold = 0.95;
const bool wb_intensity = false;

int main() {
    /*
    std::vector<float> a = {1, 2, 3, 4, 5};
    std::vector<float> b = {2, 4, 6, 8, 10};
    Image ia(1, 5, 10, a);
    Image ib(1, 5, 10, b);

    Image ad = Image::add(ia, ib);
    Image sb = Image::sub(ia, ib);
    Image mu = Image::mul(ia, ib);

    for (float f : ad.get_data()) std::cout << f << " ";
    std::cout << '\n';
    for (float f : sb.get_data()) std::cout << f << " ";
    std::cout << '\n';
    for (float f : mu.get_data()) std::cout << f << " ";
    std::cout << '\n';

    for (size_t i = 0; i < a.size(); i++) {
        std::cout << ad.get_data_at(i) << '\t' <<
            sb.get_data_at(i) << '\t' <<
            mu.get_data_at(i) << '\n';
    }

    return 0;
    */

    std::ifstream fin(input_fname);
    std::vector<Image> imgs;

#if TEST
    for (int cur_img = 0; cur_img < num_imgs; cur_img++) {
        std::vector<float> rgb(img_size * 3);
        for (float& i : rgb) fin >> i;

        Image img(width, height, 8, rgb);
        imgs.push_back(img);
    }

    MertensExposure m(imgs);
    Image final = m.fuse();
    final.write_ppm(output_prefix + "mertens");

    return 0;
#else
    for (int cur_img = 0; cur_img < num_imgs; cur_img++) {
        std::vector<float> bayer(img_size);
        for (float &i : bayer) {
            fin >> i;
        }

        Image img(width, height, 10, bayer);
        img.write_ppm(output_prefix + "bayer_" + std::to_string(cur_img));

        /*
        Image lconv = MertensExposure::convolve_laplacian(img);
        lconv.write_ppm("lconv_" + std::to_string(cur_img));

        Image gconv = MertensExposure::convolve_gaussian(img);
        gconv.write_ppm("gconv_" + std::to_string(cur_img));

        Image expos_w = MertensExposure::compute_exposure_weight(img);
        expos_w.write_ppm("expos_w" + std::to_string(cur_img));

        Image contrast_w = MertensExposure::compute_contrast_weight(img);
        contrast_w.write_ppm("contrast_w" + std::to_string(cur_img));

        Image total_w = Image::mul(expos_w, contrast_w);
        total_w.write_ppm("total_w" + std::to_string(cur_img));

        continue;
        */

        img.black_white_norm(white_level, black_level);
        img.write_ppm(output_prefix + "norm_" + std::to_string(cur_img));

        img.gray_world_wb(wb_intensity, wb_intensity_threshold);
        img.write_ppm(output_prefix + "wb_" + std::to_string(cur_img));

        img.debayer();
        img.write_ppm(output_prefix + std::to_string(cur_img));

        // imgs.push_back(img);
        if (cur_img > 0 && cur_img < 4) imgs.push_back(img);
    }

    MertensExposure m(imgs);
    Image final = m.fuse();
    /*
    final.write_ppm(output_prefix + "mertens_norm");

    final.gray_world_wb(wb_intensity, wb_intensity_threshold);
    final.write_ppm(output_prefix + "mertens_wb");

    final.debayer();
    */
    final.write_ppm(output_prefix + "mertens");

    return 0;
#endif
}
