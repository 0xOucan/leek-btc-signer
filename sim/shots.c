/*
 * Screenshots of every screen reachable from Home, for review without
 * hardware: build/shots/<profile>/NN-<SCREEN>.ppm.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "screens.h"
#include "shot.h"
#include "ui.h"

static int n;
static char dir[256];

static void save(void)
{
    char path[512];
    ui_render();
    snprintf(path, sizeof path, "%s/%02d-%s.ppm", dir, n++, ui_top()->name);
    if (shot_write_ppm(path, GFX_MONO ? 4 : 2) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
    }
}

/* A menu reachable two ways (Tools, from Home and from Create a seed) is
 * walked once. */
static const char *seen[64];
static int nseen;

static bool first_visit(const char *name)
{
    for (int i = 0; i < nseen; i++)
        if (strcmp(seen[i], name) == 0) return false;
    if (nseen < 64) seen[nseen++] = name;
    return true;
}

static void walk(int depth)
{
    save();
    const menu_t *m = ui_top()->input == menu_input ? ui_top()->ctx : NULL;
    if (!m || depth > 6 || !first_visit(ui_top()->name)) return;
    for (int i = 0; i < m->n; i++) {
        int d = ui_depth();
        ui_input(IN_OK);
        if (ui_depth() == d + 1) {
            walk(depth + 1);
            /* The I/O test counts BACK rather than obeying it. */
            if (strcmp(ui_top()->name, "IO_TEST") == 0) ui_pop(); else ui_input(IN_BACK);
        }
        ui_input(IN_DOWN);
        if (i + 1 < m->n) save();   /* the menu with each row selected */
    }
}

int main(int argc, char **argv)
{
    snprintf(dir, sizeof dir, "%s", argc > 1 ? argv[1] : "build/shots/" GFX_PROFILE_NAME);
    mkdir(dir, 0755);
    screens_set_version("dev", "S3CAM", GFX_PROFILE_NAME, "sim");
    ui_init(&SCREEN_HOME);
    walk(0);
    printf("%d screenshots in %s\n", n, dir);
    return 0;
}
