#ifndef MERTENS_H
#define MERTENS_H

#include "image.h"

typedef struct {
    u32 width;
    u32 height;
    u32 depth;
    u32 img_size;
    u32 num_imgs;
    u32 num_lvls;
    u32 pmax;

    Image* imgs;
} MertensExposure;

void mertens_init(MertensExposure* m, Image* imgs, u32 num_imgs);
Image* mertens_fuse(MertensExposure* m, Image* out);

#endif
