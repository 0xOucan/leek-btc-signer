/*
 * Desktop simulator (T5b): the firmware's UI in an SDL window, at the
 * profile's exact resolution and colours.
 *
 *   Up / Down      encoder turn
 *   Enter, Space   encoder push (OK)
 *   Esc, Backspace BACK; hold 3 s for HOLD, as on the device
 *   S              save a screenshot (leek-sim-NN.ppm)
 *   Q              quit
 *
 * BACK goes through the same button state machine as the firmware, so the
 * release-to-fire and hold timing are the real ones.
 */
#include <SDL2/SDL.h>
#include <stdio.h>

#include "build-info-sim.h"
#include "input.h"
#include "screens.h"
#include "settings.h"
#include "views-splash.h"
#include "shot.h"
#include "ui.h"

#define SCALE (GFX_MONO ? 4 : 2)

static void sink(input_ev_t ev, void *ctx)
{
    (void)ctx;
    ui_input(ev);
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Leek BTC Signer - " GFX_PROFILE_NAME,
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       GFX_W * SCALE, GFX_H * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888,
                                         SDL_TEXTUREACCESS_STREAMING, GFX_W, GFX_H);
    if (!win || !ren || !tex) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }

    screens_set_version(LBS_SIM_VERSION, "desktop", GFX_PROFILE_NAME, "sim");
    settings_reset();
    ui_init(&SCREEN_HOME);
    ui_push(&SCREEN_SPLASH);    /* OpeningSplashView, then the main menu */

    /* Only BACK needs the state machine; the rest map straight through. */
    static const input_ev_t map[1] = { IN_BACK };
    input_buttons_t back;
    input_buttons_init(&back, 1, map);
    bool back_down = false;

    uint32_t shown = 0;
    int shots = 0;
    bool running = true;
    const char *last_title = NULL;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.sym) {
                case SDLK_UP:     ui_input(IN_UP); break;
                case SDLK_DOWN:   ui_input(IN_DOWN); break;
                case SDLK_RETURN:
                case SDLK_SPACE:  ui_input(IN_OK); break;
                case SDLK_ESCAPE:
                case SDLK_BACKSPACE: back_down = true; break;
                case SDLK_q:      running = false; break;
                case SDLK_s: {
                    char path[64];
                    snprintf(path, sizeof path, "leek-sim-%02d.ppm", shots++);
                    shot_write_ppm(path, SCALE);
                    printf("saved %s\n", path);
                    break;
                }
                default: break;
                }
            }
            if (e.type == SDL_KEYUP &&
                (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_BACKSPACE)) {
                back_down = false;
            }
        }
        input_buttons_poll(&back, (int64_t)SDL_GetTicks64() * 1000, back_down ? 1u : 0u,
                           sink, NULL);

        ui_render();
        if (gfx_generation() != shown) {
            shown = gfx_generation();
            void *pixels;
            int pitch;
            SDL_LockTexture(tex, NULL, &pixels, &pitch);
            for (int y = 0; y < GFX_H; y++) {
                uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
                for (int x = 0; x < GFX_W; x++) row[x] = gfx_role_rgb(gfx_get(x, y));
            }
            SDL_UnlockTexture(tex);
        }
        if (ui_top()->name != last_title) {
            last_title = ui_top()->name;
            char t[96];
            snprintf(t, sizeof t, "Leek BTC Signer - %s - %s", GFX_PROFILE_NAME, last_title);
            SDL_SetWindowTitle(win, t);
        }
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(5);
    }
    SDL_Quit();
    return 0;
}
