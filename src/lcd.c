/* The Tamagotchi P1 LCD: a 32x16 dot matrix, driven straight out of display
 * RAM. There is no separate icon row -- see the note at the bottom.
 *
 * The E0C6S46 lays its 640 segments across 160 nibbles in two blocks, 0xE00 and
 * 0xE80. Vertically the arrangement is regular -- each screen column owns four
 * nibbles, each holding four vertically adjacent pixels:
 *
 *     nibble = COL_NIBBLE[x] + ((y >> 2) & 1) + (y >= 8 ? 80 : 0)
 *     bit    = y & 3
 *
 * Horizontally it is not regular at all, and there is no formula to find. The
 * segment lines are PCB routing: they count up in twos, skip a pair at x=8,
 * and then run *backwards* from x=16. COL_NIBBLE is that wiring, read off the
 * segment geometry in BrickEmuPy's TamagotchiP1.svg -- whose element ids are
 * `<nibble>_<bit>` and whose positions resolve to exactly 32 distinct x by 16
 * distinct y. The table reproduces all 512 cells with nothing left over.
 *
 * Assuming a formula here is the trap: `2 * x` is right for x = 0..7 and wrong
 * for the other 24 columns, which still draws a picture -- just not the one
 * the ROM meant.
 */
#include "tamarecomp/lcd.h"

static const uint8_t COL_NIBBLE[TAMA_LCD_W] = {
     0,  2,  4,  6,  8, 10, 12, 14,     /* x 0-7:   straight run          */
    18, 20, 22, 24, 26, 28, 30, 32,     /* x 8-15:  after a skipped pair  */
    72, 70, 68, 66, 64, 62, 60, 58,     /* x 16-23: and now backwards     */
    54, 52, 50, 48, 46, 44, 42, 40,     /* x 24-31: still backwards       */
};

/* Display RAM is two blocks, not one run: nibbles 0-79 sit at 0xE00 and 80-159
 * at 0xE80. */
static uint16_t nibble_addr(int n)
{
    return (uint16_t)(n < 80 ? 0xE00 + n : 0xE80 + (n - 80));
}

int tama_lcd_pixel(const tama_t *t, int x, int y)
{
    int n;

    if (x < 0 || x >= TAMA_LCD_W || y < 0 || y >= TAMA_LCD_H)
        return 0;

    n = COL_NIBBLE[x] + ((y >> 2) & 1) + (y >= 8 ? 80 : 0);
    return (t->mem[nibble_addr(n)] >> (y & 3)) & 1;
}

void tama_lcd_read(const tama_t *t, uint8_t out[TAMA_LCD_H][TAMA_LCD_W])
{
    for (int y = 0; y < TAMA_LCD_H; y++)
        for (int x = 0; x < TAMA_LCD_W; x++)
            out[y][x] = (uint8_t)tama_lcd_pixel(t, x, y);
}

/* There is no icon code here on purpose.
 *
 * The P1 looks like it has an icon row -- food, light, game, medicine and the
 * rest -- printed above and below the screen, and the obvious assumption is
 * that each is its own LCD segment in the 32 nibbles the matrix leaves unused.
 * It is not. Instrumenting every address in 0xE00-0xEFF across a run that
 * walks the whole menu shows the ROM changing 88 addresses, every one of them
 * inside the matrix and none outside it. The icon row is *drawn in the dot
 * matrix*, which is also why BrickEmuPy's face SVG defines exactly 512
 * segments and nothing else.
 *
 * tests/iconprobe.c keeps that honest: it fails if the ROM ever drives a
 * display-RAM address the matrix does not cover.
 */
