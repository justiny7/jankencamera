#include "lib.h"
#include "camera.h"
#include "display.h"
#include "mmu.h"
#include "sys_timer.h"
#include "fat.h"
#include "mertens.h"

#define WIDTH  640
#define HEIGHT 480
#define SIZE (WIDTH * HEIGHT)
// #define WIDTH  1920
// #define HEIGHT 1080

#define FILENAME "INPUT   TXT"

#define N 3
#define DEPTH 10

#define BLACK_LVL 64
#define WHITE_LVL 1023

typedef uint32_t u32;
static Image imgs[N];
static float bayer[N][SIZE];
static uint8_t buf[SIZE * 3];

static void to_fat_8_3(const char* filename, char out[11]) {
    memset(out, ' ', 11);
    int i = 0, j = 0;
    while (filename[i] && filename[i] != '.' && j < 8) {
        char c = filename[i++];
        out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
    }
    if (filename[i] == '.') {
        i++;
        j = 8;
        while (filename[i] && j < 11) {
            char c = filename[i++];
            out[j++] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
    }
}

bool sd_write_file(const char* filename, const uint8_t* data, uint32_t size) {
    char name_8_3[11];
    to_fat_8_3(filename, name_8_3);
    fat_write_file(name_8_3, data, size);
    return true;
}

u32 next_int(uint8_t** data) {
    u32 res = 0;
    uint8_t* p = *data;
    while (*p != ' ') {
        res = res * 10 + (*p - '0');
        p++;
    }

    *data = ++p;
    return res;
}

void main() {
    mmu_enable_caches();

    if (!display_init(WIDTH, HEIGHT)) {
        printk("display init failed\n");
        return;
    }

    fat_init();
    uint8_t* file_data;
    uint32_t filesize;

    fat_read_file(FILENAME, &file_data, &filesize);

    printk("file read, size: %d\n", filesize);
    for (int i = 0; i < 10; i++) {
    }

    for (int c = 0; c < N; c++) {
        for (u32 i = 0; i < SIZE; i++) {
            u32 cur = next_int(&file_data);
            bayer[c][i] = (float) cur;
        }
    }

    printk("got img data\n");

    for (int i = 0; i < N; i++) {
        Image* img = &imgs[i];
        img_init_data(img, WIDTH, HEIGHT, DEPTH, PIXEL_GRAY, bayer[i]);

        img_black_white_norm(img, WHITE_LVL, BLACK_LVL);
        img_gray_world_wb(img);
        img_debayer(img);

        /*
        for (int j = 0; j < SIZE * 3; j++) {
            buf[j] = ((uint16_t) img->data[j]) >> 2;
        }

        assert(sd_write_file("img1.txt", buf, SIZE * 3), "can't write to file");
        */
    }

    MertensExposure m;
    mertens_init(&m, imgs, N);

    Image* res = mertens_fuse(&m);
    for (int j = 0; j < SIZE * 3; j++) {
        buf[j] = ((uint16_t) res->data[j]) >> 2;
    }

    assert(sd_write_file("mert.bin", buf, SIZE * 3), "can't write to file");
}
