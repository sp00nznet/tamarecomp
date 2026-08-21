/* Find the icon segments by elimination.
 *
 * The 32x16 matrix accounts for 128 of display RAM's nibbles. Anything the ROM
 * drives *outside* those has to be an icon, so: work out which addresses the
 * matrix mapper can reach, run the device while walking the buttons, and
 * report every address that changed and is not one of them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamarecomp/lcd.h"

#define LO 0xE00
#define HI 0xEFF
#define N  (HI - LO + 1)

static uint8_t seen_set[N], seen_clear[N], is_matrix[N];

static void sample(const tama_t *t)
{
    for (int i = 0; i < N; i++) {
        uint8_t v = t->mem[LO + i] & 0xF;
        seen_set[i] |= v;
        seen_clear[i] |= (uint8_t)(~v) & 0xF;
    }
}

static void run(tama_t *t, double secs)
{
    for (int i = 0; i < (int)(secs * 16); i++) {
        tama_step(t, TAMA_OSC1_HZ / 16);
        sample(t);
    }
}

static void press(tama_t *t, uint8_t b)
{
    tama_set_buttons(t, b);
    tama_step(t, TAMA_OSC1_HZ / 5);
    tama_set_buttons(t, 0);
    run(t, 1.5);
}

/* Ask the mapper, rather than reimplementing it: light one nibble at a time
 * and see whether any pixel notices. */
static void find_matrix_addresses(void)
{
    tama_t *z = calloc(1, sizeof(tama_t));
    if (!z)
        exit(2);
    for (int i = 0; i < N; i++) {
        memset(z->mem, 0, sizeof z->mem);
        z->mem[LO + i] = 0xF;
        for (int y = 0; y < TAMA_LCD_H && !is_matrix[i]; y++)
            for (int x = 0; x < TAMA_LCD_W; x++)
                if (tama_lcd_pixel(z, x, y)) {
                    is_matrix[i] = 1;
                    break;
                }
    }
    free(z);
}

int main(void)
{
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;
    tama_reset(t);

    run(t, 15.0);
    for (int i = 0; i < 12; i++)        /* left cycles the menu icons */
        press(t, TAMA_BTN_LEFT);
    press(t, TAMA_BTN_MIDDLE);          /* select whatever is highlighted */
    run(t, 5.0);
    press(t, TAMA_BTN_RIGHT);           /* and back out */
    run(t, 5.0);

    find_matrix_addresses();

    int matrix_addrs = 0, live = 0, outside = 0;
    for (int i = 0; i < N; i++)
        matrix_addrs += is_matrix[i];

    printf("matrix occupies %d addresses in 0x%03X-0x%03X\n",
           matrix_addrs, LO, HI);
    printf("after %llus of runtime:\n\n",
           (unsigned long long)t->cycles / TAMA_OSC1_HZ);

    for (int i = 0; i < N; i++) {
        uint8_t toggled = seen_set[i] & seen_clear[i];
        if (!toggled)
            continue;
        live++;
        if (is_matrix[i])
            continue;
        outside++;
        printf("  0x%03X  outside the matrix, bits", LO + i);
        for (int b = 0; b < 4; b++)
            if (toggled & (1 << b))
                printf(" %d", b);
        printf("   (ever set: %X)\n", seen_set[i]);
    }

    printf("\n%d addresses changed, %d of them outside the matrix\n",
           live, outside);

    if (live < 32) {
        printf("FAIL: the device barely drew anything; this proves nothing\n");
        return 1;
    }
    if (outside) {
        printf("FAIL: the ROM drives display RAM the matrix does not cover.\n"
               "      Those are icon segments and src/lcd.c should render them.\n");
        return 1;
    }
    printf("ok: the icon row is drawn inside the matrix, not on separate segments\n");
    free(t);
    return 0;
}
