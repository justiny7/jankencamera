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

#define VERBOSE 1

typedef uint32_t u32;
static Image imgs[N];
static uint16_t bayer[N][SIZE];

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

#if VERBOSE
static uint32_t last_t;
#define now(...) \
    do { \
        printk(__VA_ARGS__); \
        last_t = sys_timer_get_usec(); \
    } while(false)
#define elapsed() printk("elapsed (us): %d\n", sys_timer_get_usec() - last_t)
#else
#define now(...)
#define elapsed()
#endif

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

    for (int c = 0; c < N; c++) {
        for (u32 i = 0; i < SIZE; i++) {
            bayer[c][i] = (uint16_t) next_int(&file_data);
        }
    }

    printk("got img data\n");

    img_kernel_init();
    for (int i = 0; i < N; i++) {
        Image* img = &imgs[i];
        img_init_bayer(img, WIDTH, HEIGHT, DEPTH, (uint8_t*) bayer[i], BAYER_RGGB);

        now("black level sub + white balance\n");
        img_bw_norm_gray_world_wb(img, WHITE_LVL, BLACK_LVL);
        elapsed();

        now("debayer\n");
        img_debayer_fast(img, 0, true);
        elapsed();

        char* fn = "TMP     PPM";
        fn[3] = '0' + i;
        img_save_ppm(img, fn);
    }

    MertensExposure m;
    mertens_init(&m, imgs, N);

    now("mertens\n");
    Image* res = mertens_fuse(&m);
    elapsed();

    printk("write ppm...\n");
    img_save_ppm(res, "OUT     PPM");
}
