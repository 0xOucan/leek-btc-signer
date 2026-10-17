/* ui.c and the screen tree: navigation, BACK, HOLD, leave hooks, rendering. */
#include <string.h>

#include "check.h"
#include "gfx.h"
#include "screens.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

static int leaves;
static void count_leave(const screen_t *s) { (void)s; leaves++; }
static const screen_t SECRET = { .name = "SECRET", .leave = count_leave };

static const char *top(void) { return ui_top()->name; }

static void press(input_ev_t ev, int n)
{
    for (int i = 0; i < n; i++) ui_input(ev);
}

static int lit_pixels(void)
{
    int n = 0;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++)
            n += gfx_get(x, y) != C_BG;
    return n;
}

/* Render whatever is on top and check something was drawn in the title bar
 * and below it. */
static void check_renders(void)
{
    ui_render();
    int title = 0, body = 0;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++)
            if (gfx_get(x, y) != C_BG) (*(y < THEME.top_h ? &title : &body))++;
    CHECK(title > 0, "%s: nothing in the title bar", top());
    CHECK(body > 0, "%s: nothing below the title", top());
}

/* Visit every screen reachable from here, depth first, and come back. */
static int visited;
static void walk(int depth)
{
    check_renders();
    visited++;
    if (depth > 6 || strstr(top(), "IOTestView")) return;
    const menu_t *m = ui_top()->input == menu_input ? ui_top()->ctx : NULL;
    if (!m) return;
    press(IN_UP, m->n);     /* a covered menu keeps its selection */
    for (int i = 0; i < m->n; i++) {
        const char *here = top();
        int d = ui_depth();
        ui_input(IN_OK);
        if (ui_depth() == d + 1) {
            walk(depth + 1);
            /* The I/O test counts BACK rather than obeying it. */
            if (strcmp(top(), "IOTestView") == 0) ui_pop(); else ui_input(IN_BACK);
        }
        CHECK(ui_depth() == d && strcmp(top(), here) == 0, "BACK returned to %s (on %s, depth %d/%d)", here,
              top(), ui_depth(), d);
        ui_input(IN_DOWN);
    }
}

int main(void)
{
    settings_reset();       /* SeedSigner's defaults, as the firmware sets at boot */
    ui_init(&SCREEN_HOME);
    CHECK(strcmp(top(), "MainMenuView") == 0 && ui_depth() == 1, "starts at home");

    ui_input(IN_BACK);
    CHECK(ui_depth() == 1, "BACK at home stays home");

    press(IN_DOWN, 1);
    ui_input(IN_OK);
    CHECK(strcmp(top(), "LoadSeedView") == 0, "Seeds opens Load A Seed, got %s", top());
    press(IN_DOWN, 3);
    ui_input(IN_OK);
    CHECK(strcmp(top(), "ToolsMenuView") == 0, "Create a seed opens Tools, got %s", top());
    CHECK(ui_depth() == 3, "depth 3");
    ui_input(IN_HOLD);
    CHECK(ui_depth() == 1 && strcmp(top(), "MainMenuView") == 0, "HOLD goes home");

    /* Selection is kept on the covered menu and reset on a fresh visit. */
    press(IN_DOWN, 1);
    ui_input(IN_OK);
    press(IN_DOWN, 2);
    ui_input(IN_OK);
    ui_input(IN_BACK);
    CHECK(((menu_t *)ui_top()->ctx)->sel == 2, "covered menu kept its selection");
    ui_input(IN_BACK);
    ui_input(IN_OK);
    CHECK(((menu_t *)ui_top()->ctx)->sel == 0, "pushed menu starts at the top");
    ui_home();

    /* The menu does not run off either end. */
    press(IN_UP, 5);
    CHECK(((menu_t *)ui_top()->ctx)->sel == 0, "UP stops at the first item");
    press(IN_DOWN, 20);
    CHECK(((menu_t *)ui_top()->ctx)->sel == 3, "DOWN stops at the last item");
    ui_home();
    press(IN_UP, 5);

    /* leave runs on every path off the stack. */
    leaves = 0;
    ui_push(&SECRET);
    ui_input(IN_BACK);
    CHECK(leaves == 1, "leave on BACK");
    ui_push(&SECRET);
    ui_input(IN_HOLD);
    CHECK(leaves == 2, "leave on HOLD");
    ui_push(&SECRET);
    ui_push(&SECRET);
    ui_home();
    CHECK(leaves == 4, "leave for every screen ui_home pops");

    for (int i = 0; i < UI_STACK_MAX - 1; i++) CHECK(ui_push(&SECRET), "push %d", i);
    CHECK(!ui_push(&SECRET), "the stack refuses to overflow");
    ui_home();

    /* T28: ten levels down, BACK walks home one level at a time and HOLD
       in one step; every level's leave runs on either way, on a replace,
       and when the whole stack is reset. */
    leaves = 0;
    for (int i = 0; i < 10; i++) ui_push(&SECRET);
    for (int i = 0; i < 10; i++) ui_input(IN_BACK);
    CHECK(ui_depth() == 1 && leaves == 10, "BACK x10: depth %d, %d leaves", ui_depth(), leaves);
    leaves = 0;
    for (int i = 0; i < 10; i++) ui_push(&SECRET);
    ui_input(IN_HOLD);
    CHECK(ui_depth() == 1 && leaves == 10, "HOLD: depth %d, %d leaves", ui_depth(), leaves);
    leaves = 0;
    ui_push(&SECRET);
    ui_replace(&SECRET);
    CHECK(ui_depth() == 2 && leaves == 1, "replace leaves the replaced screen");
    ui_push(&SECRET);
    ui_init(&SCREEN_HOME);
    CHECK(ui_depth() == 1 && leaves == 3, "ui_init leaves every screen, %d", leaves);

    /* I/O test counts BACK instead of leaving; HOLD still leaves. */
    screens_set_version("test", "HOST", GFX_PROFILE_NAME, "test");
    press(IN_DOWN, 3);
    ui_input(IN_OK);
    for (int i = 0; i < 12 && strcmp(((const menu_t *)ui_top()->ctx)->items[((const menu_t *)ui_top()->ctx)->sel].label, "I/O test") != 0; i++) {
        ui_input(IN_DOWN);
    }
    ui_input(IN_OK);
    CHECK(strcmp(top(), "IOTestView") == 0, "I/O test, got %s", top());
    ui_input(IN_BACK);
    CHECK(strcmp(top(), "IOTestView") == 0, "BACK is counted, not obeyed");
    check_renders();
    ui_input(IN_HOLD);
    CHECK(ui_depth() == 1, "HOLD leaves the I/O test");

    /* Every reachable screen draws a title and a body on this profile. */
    visited = 0;
    walk(0);
    CHECK(visited >= 20, "walked %d screens", visited);
    CHECK(lit_pixels() > 0, "something on screen at the end");

    return CHECK_DONE("test_ui [" GFX_PROFILE_NAME "]");
}
