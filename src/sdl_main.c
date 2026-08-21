/* A Tamagotchi in a window.
 *
 *   tama-sdl
 *   tama-sdl --shot out.bmp [seconds] [buttons]
 *
 * Click the three buttons, or use keys 1/2/3 (or a/s/d). Escape quits.
 *
 * --shot runs without pacing and writes one frame to a BMP. It exists because
 * a renderer you cannot look at is a renderer you have not tested; it also
 * works under SDL_VIDEODRIVER=dummy.
 *
 * Nothing here reaches into the emulator: it calls the same three entry points
 * the terminal frontend does -- tama_audio_step to advance time and pull
 * samples, tama_lcd_read to get pixels, tama_set_buttons for input. The core
 * has no idea it is being drawn.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Take main() back from SDL2main. Its WinMain shim forces the Windows
 * subsystem, which throws away stderr -- and stderr is where a trap gets
 * reported. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "tamarecomp/lcd.h"

#define SCALE   14                     /* pixel size of one LCD dot */
#define BEZEL   18
#define LCD_X   BEZEL
#define LCD_Y   BEZEL
#define LCD_W   (TAMA_LCD_W * SCALE)
#define LCD_H   (TAMA_LCD_H * SCALE)
#define BTN_R   26
#define BTN_Y   (LCD_Y + LCD_H + BEZEL + BTN_R)
#define WIN_W   (LCD_W + 2 * BEZEL)
#define WIN_H   (BTN_Y + BTN_R + BEZEL)

#define RATE    22050
#define FPS     60
#define SAMPLES_PER_FRAME (RATE / FPS)

/* The shell, the unlit LCD, and a lit dot. The middle one is the colour a
 * cheap 1996 STN panel goes when it is doing nothing. */
static const SDL_Color SHELL = { 0x2A, 0x2D, 0x34, 0xFF };
static const SDL_Color LCD_OFF = { 0x8C, 0xA1, 0x6B, 0xFF };
static const SDL_Color LCD_ON = { 0x1C, 0x22, 0x14, 0xFF };
static const SDL_Color BTN = { 0x6E, 0x74, 0x80, 0xFF };
static const SDL_Color BTN_DOWN = { 0xC8, 0x50, 0x50, 0xFF };

static const int BTN_CX[3] = {
    WIN_W / 2 - 90, WIN_W / 2, WIN_W / 2 + 90,
};
static const uint8_t BTN_BIT[3] = {
    TAMA_BTN_LEFT, TAMA_BTN_MIDDLE, TAMA_BTN_RIGHT,
};

static void set_color(SDL_Renderer *r, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
}

/* A filled circle, drawn as horizontal spans. SDL2 has no circle primitive and
 * three buttons do not justify pulling in SDL2_gfx for one. */
static void fill_circle(SDL_Renderer *r, int cx, int cy, int rad)
{
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = (int)(SDL_sqrt((double)(rad * rad - dy * dy)) + 0.5);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static int hit_button(int mx, int my)
{
    for (int i = 0; i < 3; i++) {
        int dx = mx - BTN_CX[i], dy = my - BTN_Y;
        if (dx * dx + dy * dy <= BTN_R * BTN_R)
            return i;
    }
    return -1;
}

static uint8_t key_button(SDL_Keycode k)
{
    switch (k) {
    case SDLK_1: case SDLK_a: case SDLK_KP_1: return TAMA_BTN_LEFT;
    case SDLK_2: case SDLK_s: case SDLK_KP_2: return TAMA_BTN_MIDDLE;
    case SDLK_3: case SDLK_d: case SDLK_KP_3: return TAMA_BTN_RIGHT;
    default:                                  return 0;
    }
}

static void render(SDL_Renderer *r, const uint8_t px[TAMA_LCD_H][TAMA_LCD_W],
                   uint8_t held)
{
    set_color(r, SHELL);
    SDL_RenderClear(r);

    SDL_Rect lcd = { LCD_X, LCD_Y, LCD_W, LCD_H };
    set_color(r, LCD_OFF);
    SDL_RenderFillRect(r, &lcd);

    /* One rect per lit dot, inset by a pixel so the panel keeps its grid. */
    set_color(r, LCD_ON);
    for (int y = 0; y < TAMA_LCD_H; y++)
        for (int x = 0; x < TAMA_LCD_W; x++) {
            if (!px[y][x])
                continue;
            SDL_Rect d = { LCD_X + x * SCALE, LCD_Y + y * SCALE,
                           SCALE - 1, SCALE - 1 };
            SDL_RenderFillRect(r, &d);
        }

    for (int i = 0; i < 3; i++) {
        set_color(r, (held & BTN_BIT[i]) ? BTN_DOWN : BTN);
        fill_circle(r, BTN_CX[i], BTN_Y, BTN_R);
    }
    SDL_RenderPresent(r);
}

int main(int argc, char **argv)
{
    const char *shot = NULL;
    int shot_secs = 5;
    uint8_t shot_btn = 0;

    if (argc > 2 && strcmp(argv[1], "--shot") == 0) {
        shot = argv[2];
        if (argc > 3)
            shot_secs = atoi(argv[3]);
        if (argc > 4)
            shot_btn = (uint8_t)atoi(argv[4]);
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("Tamagotchi",
                                       SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED,
                                       WIN_W, WIN_H, 0);
    /* No flags: SDL picks accelerated where it can and software where it
     * cannot, which is what lets --shot work under SDL_VIDEODRIVER=dummy.
     * Demanding ACCELERATED just fails there. */
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, 0);
    if (!win || !ren) {
        fprintf(stderr, "SDL window: %s\n", SDL_GetError());
        return 1;
    }

    /* Queued audio rather than a callback: the emulator already produces
     * samples in step with its own clock, so there is nothing to synchronise
     * and no lock to get wrong. */
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    SDL_AudioDeviceID audio = shot ? 0
                            : SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (audio)
        SDL_PauseAudioDevice(audio, 0);

    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;
    tama_reset(t);

    tama_audio_t au = { 0.0, 0 };
    int16_t buf[SAMPLES_PER_FRAME];
    uint8_t px[TAMA_LCD_H][TAMA_LCD_W];
    uint8_t mouse_held = 0, key_held = 0;
    int running = 1;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (e.key.keysym.sym == SDLK_ESCAPE)
                    running = 0;
                key_held |= key_button(e.key.keysym.sym);
                break;
            case SDL_KEYUP:
                key_held &= (uint8_t)~key_button(e.key.keysym.sym);
                break;
            case SDL_MOUSEBUTTONDOWN: {
                int i = hit_button(e.button.x, e.button.y);
                if (i >= 0)
                    mouse_held |= BTN_BIT[i];
                break;
            }
            case SDL_MOUSEBUTTONUP:
                mouse_held = 0;
                break;
            default:
                break;
            }
        }

        if (shot)
            mouse_held = shot_btn;
        tama_set_buttons(t, (uint8_t)(mouse_held | key_held));

        /* Audio drives the clock: one frame of samples is exactly one frame of
         * Tamagotchi time. */
        for (int i = 0; i < SAMPLES_PER_FRAME; i++)
            buf[i] = tama_audio_step(t, &au, RATE);
        if (t->trapped) {
            fprintf(stderr, "trapped at 0x%04X\n", t->pc);
            running = 0;
        }

        if (audio) {
            /* If the queue runs long the machine is outpacing real time; drop
             * the backlog rather than drift further behind. */
            if (SDL_GetQueuedAudioSize(audio) > 4 * sizeof buf)
                SDL_ClearQueuedAudio(audio);
            SDL_QueueAudio(audio, buf, sizeof buf);
        }

        tama_lcd_read(t, px);
        render(ren, px, (uint8_t)(mouse_held | key_held));

        if (shot) {
            if (t->cycles < (uint64_t)shot_secs * TAMA_OSC1_HZ)
                continue;                      /* run flat out until the mark */
            SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(
                0, WIN_W, WIN_H, 32, SDL_PIXELFORMAT_ARGB8888);
            SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888,
                                 s->pixels, s->pitch);
            SDL_SaveBMP(s, shot);
            SDL_FreeSurface(s);
            printf("%s: %dx%d at %.1fs\n", shot, WIN_W, WIN_H,
                   (double)t->cycles / TAMA_OSC1_HZ);
            running = 0;
            continue;
        }
        SDL_Delay(1000 / FPS);
    }

    if (audio)
        SDL_CloseAudioDevice(audio);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(t);
    return 0;
}
