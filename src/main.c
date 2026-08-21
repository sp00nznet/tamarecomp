/* A Tamagotchi in your terminal.
 *
 *   tama <rom-is-baked-in> [seconds]
 *
 * Runs the recompiled ROM in real time and redraws the 32x16 dot matrix
 * whenever it changes. Two pixel rows share one terminal line via the
 * half-block character, so the screen keeps its aspect ratio.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamarecomp/lcd.h"

#ifdef _WIN32
#  include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static void enable_ansi(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}
#else
#  include <time.h>
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static void enable_ansi(void) {}
#endif

#define FPS 10

static void draw(const uint8_t px[TAMA_LCD_H][TAMA_LCD_W], uint64_t secs)
{
    printf("\033[H\033[2J");
    printf("  .--------------------------------.\n");
    for (int y = 0; y < TAMA_LCD_H; y += 2) {
        fputs("  |", stdout);
        for (int x = 0; x < TAMA_LCD_W; x++) {
            int top = px[y][x], bot = px[y + 1][x];
            /* U+2580 upper half, U+2584 lower half, U+2588 full */
            fputs(top && bot ? "█" : top ? "▀" : bot ? "▄" : " ",
                  stdout);
        }
        fputs("|\n", stdout);
    }
    printf("  '--------------------------------'\n");
    printf("   %llu:%02llu:%02llu elapsed        ctrl-c to quit\n",
           (unsigned long long)secs / 3600,
           (unsigned long long)(secs / 60) % 60,
           (unsigned long long)secs % 60);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    long limit = (argc > 1) ? strtol(argv[1], NULL, 10) : 0;   /* 0 = forever */
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;

    enable_ansi();
    tama_reset(t);

    uint8_t px[TAMA_LCD_H][TAMA_LCD_W], prev[TAMA_LCD_H][TAMA_LCD_W];
    memset(prev, 0xFF, sizeof prev);

    for (;;) {
        tama_step(t, TAMA_OSC1_HZ / FPS);
        if (t->trapped) {
            printf("\ntrapped at 0x%04X\n", t->pc);
            return 1;
        }

        tama_lcd_read(t, px);
        if (memcmp(px, prev, sizeof px) != 0) {
            draw(px, t->cycles / TAMA_OSC1_HZ);
            memcpy(prev, px, sizeof px);
        }

        if (limit && t->cycles >= (uint64_t)limit * TAMA_OSC1_HZ)
            break;
        sleep_ms(1000 / FPS);
    }
    free(t);
    return 0;
}
