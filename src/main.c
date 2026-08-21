/* A Tamagotchi in your terminal.
 *
 *   tama [seconds]        (0 or omitted: run until ctrl-c)
 *
 * Runs the recompiled ROM in real time and redraws the 32x16 dot matrix
 * whenever it changes. Two pixel rows share one terminal line via the
 * half-block characters, so the screen keeps its aspect ratio.
 *
 * Keys 1/2/3 (or a/s/d) are the three buttons.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamarecomp/lcd.h"

#ifdef _WIN32
#  include <conio.h>
#  include <windows.h>
static void sleep_ms(int ms) { Sleep(ms); }
static void term_setup(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
}
static void term_restore(void) {}
static int poll_key(void) { return _kbhit() ? _getch() : -1; }
#else
#  include <fcntl.h>
#  include <termios.h>
#  include <time.h>
#  include <unistd.h>
static struct termios saved;
static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static void term_restore(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
}
static void term_setup(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &saved);
    raw = saved;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);
    atexit(term_restore);
}
static int poll_key(void)
{
    unsigned char c;
    return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}
#endif

#define FPS 20

/* How long a keystroke holds the button down. A terminal gives us a press but
 * never a release, and the ROM debounces its inputs, so a tap that lasts one
 * frame can be missed entirely. */
#define HOLD_FRAMES 4

static uint8_t key_to_button(int c)
{
    switch (c) {
    case '1': case 'a': case 'A': return TAMA_BTN_LEFT;
    case '2': case 's': case 'S': return TAMA_BTN_MIDDLE;
    case '3': case 'd': case 'D': return TAMA_BTN_RIGHT;
    default:                      return 0;
    }
}

static void draw(const uint8_t px[TAMA_LCD_H][TAMA_LCD_W], uint64_t secs,
                 uint8_t held)
{
    printf("\033[H\033[2J");
    printf("  .--------------------------------.\n");
    for (int y = 0; y < TAMA_LCD_H; y += 2) {
        fputs("  |", stdout);
        for (int x = 0; x < TAMA_LCD_W; x++) {
            int top = px[y][x], bot = px[y + 1][x];
            fputs(top && bot ? "█" : top ? "▀" : bot ? "▄" : " ",
                  stdout);
        }
        fputs("|\n", stdout);
    }
    printf("  '--------------------------------'\n");
    printf("       %s   %s   %s\n",
           (held & TAMA_BTN_LEFT)   ? "[1]" : " 1 ",
           (held & TAMA_BTN_MIDDLE) ? "[2]" : " 2 ",
           (held & TAMA_BTN_RIGHT)  ? "[3]" : " 3 ");
    printf("   %llu:%02llu:%02llu elapsed          q to quit\n",
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

    term_setup();
    tama_reset(t);

    uint8_t px[TAMA_LCD_H][TAMA_LCD_W], prev[TAMA_LCD_H][TAMA_LCD_W];
    memset(prev, 0xFF, sizeof prev);
    uint8_t held = 0;
    int hold_left = 0;

    for (;;) {
        int c;
        while ((c = poll_key()) >= 0) {
            if (c == 'q' || c == 'Q' || c == 3)
                goto done;
            uint8_t b = key_to_button(c);
            if (b) {
                held = b;
                hold_left = HOLD_FRAMES;
            }
        }
        if (hold_left && --hold_left == 0)
            held = 0;
        tama_set_buttons(t, held);

        tama_step(t, TAMA_OSC1_HZ / FPS);
        if (t->trapped) {
            printf("\ntrapped at 0x%04X\n", t->pc);
            return 1;
        }

        tama_lcd_read(t, px);
        static uint8_t last_held = 0xFF;
        if (memcmp(px, prev, sizeof px) != 0 || held != last_held) {
            draw(px, t->cycles / TAMA_OSC1_HZ, held);
            memcpy(prev, px, sizeof px);
            last_held = held;
        }

        if (limit && t->cycles >= (uint64_t)limit * TAMA_OSC1_HZ)
            break;
        sleep_ms(1000 / FPS);
    }
done:
    term_restore();
    free(t);
    return 0;
}
