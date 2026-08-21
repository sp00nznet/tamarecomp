/* A Tamagotchi in your terminal.
 *
 *   tama [seconds]                 run it; 1/2/3 are the buttons, q quits
 *   tama --record out.wav [secs]   run flat out and write what the buzzer did
 *
 * Two pixel rows share one terminal line via the half-block characters, so the
 * 32x16 screen keeps its aspect ratio.
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

/* ---------------------------------------------------------------- recording */

#define RATE 22050

static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f);
                                         fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

/* Run the machine flat out and write the buzzer to a mono 16-bit WAV.
 *
 * The E0C6S46's buzzer is a square wave and nothing more -- one of eight tones
 * or silence -- so no audio library is involved. tama_audio_step does the
 * work and drives the machine's clock at the same time. */
static int record(const char *path, int seconds)
{
    tama_t *t = calloc(1, sizeof(tama_t));
    FILE *f = fopen(path, "wb");
    if (!t || !f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 2;
    }
    tama_reset(t);

    long nsamples = (long)seconds * RATE;
    unsigned bytes = (unsigned)nsamples * 2;

    fwrite("RIFF", 1, 4, f);  put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);  put32(f, 16);
    put16(f, 1); put16(f, 1);                 /* PCM, mono */
    put32(f, RATE); put32(f, RATE * 2);
    put16(f, 2); put16(f, 16);
    fwrite("data", 1, 4, f);  put32(f, bytes);

    tama_audio_t au = { 0.0, 0 };
    long rang = 0;
    for (long i = 0; i < nsamples; i++) {
        int16_t s = tama_audio_step(t, &au, RATE);
        if (s)
            rang++;
        put16(f, (unsigned)(s & 0xFFFF));
    }
    fclose(f);
    printf("%s: %d s, buzzer sounding for %.2f s\n",
           path, seconds, (double)rang / RATE);
    free(t);
    return 0;
}

/* ------------------------------------------------------------------ display */

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
                 uint8_t held, unsigned hz)
{
    printf("\033[H\033[2J");
    printf("  .--------------------------------.\n");
    for (int y = 0; y < TAMA_LCD_H; y += 2) {
        fputs("  |", stdout);
        for (int x = 0; x < TAMA_LCD_W; x++) {
            int top = px[y][x], bot = px[y + 1][x];
            fputs(top && bot ? "█" : top ? "▀" : bot ? "▄" : " ", stdout);
        }
        fputs("|\n", stdout);
    }
    printf("  '--------------------------------'\n");
    printf("       %s   %s   %s      %s\n",
           (held & TAMA_BTN_LEFT)   ? "[1]" : " 1 ",
           (held & TAMA_BTN_MIDDLE) ? "[2]" : " 2 ",
           (held & TAMA_BTN_RIGHT)  ? "[3]" : " 3 ",
           hz ? "♪" : " ");
    printf("   %llu:%02llu:%02llu elapsed          q to quit\n",
           (unsigned long long)secs / 3600,
           (unsigned long long)(secs / 60) % 60,
           (unsigned long long)secs % 60);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--record") == 0)
        return record(argv[2], (argc > 3) ? atoi(argv[3]) : 10);

    long limit = (argc > 1) ? strtol(argv[1], NULL, 10) : 0;   /* 0 = forever */
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;

    term_setup();
    tama_reset(t);

    uint8_t px[TAMA_LCD_H][TAMA_LCD_W], prev[TAMA_LCD_H][TAMA_LCD_W];
    memset(prev, 0xFF, sizeof prev);
    uint8_t held = 0, last_held = 0xFF;
    unsigned last_hz = 0xFFFFFFFFu;
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

        unsigned hz = tama_buzzer_hz(t);
        tama_lcd_read(t, px);
        if (memcmp(px, prev, sizeof px) != 0 || held != last_held
                || hz != last_hz) {
            draw(px, t->cycles / TAMA_OSC1_HZ, held, hz);
            memcpy(prev, px, sizeof px);
            last_held = held;
            last_hz = hz;
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
