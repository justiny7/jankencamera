#include "image.h"
#include "mertens.h"
#include "scoped_timer.h"

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
const int num_imgs = 3;
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

        {
            ScopedTimer t("Black level subtraction");
            img.black_white_norm(white_level, black_level);
        }

        {
            ScopedTimer t("White balance");
            img.gray_world_wb(wb_intensity, wb_intensity_threshold);
        }

        {
            ScopedTimer t("Debayer");
            img.debayer();
        }

        imgs.push_back(img);

        img.convert_depth(8);
        // img.write_ppm(output_prefix + std::to_string(cur_img));
    }

    MertensExposure m(imgs);
    Image final;
    {
        ScopedTimer t("Mertens");
        final = m.fuse();
    }

    final.convert_depth(8);
    final.write_ppm(output_prefix + "mertens_chroma");

    ScopedTimer::print_stats();

    return 0;
#endif
}
