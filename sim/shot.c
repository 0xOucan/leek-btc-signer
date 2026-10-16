#include "shot.h"

#include <stdio.h>

#include "gfx.h"

int shot_write_ppm(const char *path, int scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", GFX_W * scale, GFX_H * scale);
    for (int y = 0; y < GFX_H * scale; y++) {
        for (int x = 0; x < GFX_W * scale; x++) {
            uint32_t rgb = gfx_role_rgb(gfx_get(x / scale, y / scale));
            unsigned char px[3] = { (unsigned char)(rgb >> 16),
                                    (unsigned char)(rgb >> 8),
                                    (unsigned char)rgb };
            fwrite(px, 1, 3, f);
        }
    }
    return fclose(f);
}
