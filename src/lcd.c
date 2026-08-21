/* The Tamagotchi P1 LCD: a 32x16 dot matrix driven straight out of display RAM.
 *
 * The E0C6S46 lays its 640 segments across 160 nibbles in two blocks, 0xE00 and
 * 0xE80. For the dot matrix the arrangement is column-major in fours: each
 * screen column owns four nibbles, and each nibble holds four vertically
 * adjacent pixels.
 *
 *     addr = (y >= 8 ? 0xE80 : 0xE00) + 2 * x + ((y >> 2) & 1)
 *     bit  = y & 3
 *
 * That is not a guess -- it is read off the segment geometry in BrickEmuPy's
 * TamagotchiP1.svg, whose element ids are `<nibble>_<bit>` and whose positions
 * lay out as exactly 32 distinct x by 16 distinct y, all 512 cells accounted
 * for.
 */
#include "tamarecomp/lcd.h"

int tama_lcd_pixel(const tama_t *t, int x, int y)
{
    uint16_t addr;

    if (x < 0 || x >= TAMA_LCD_W || y < 0 || y >= TAMA_LCD_H)
        return 0;

    addr = (uint16_t)((y >= 8 ? 0xE80 : 0xE00) + 2 * x + ((y >> 2) & 1));
    return (t->mem[addr] >> (y & 3)) & 1;
}

void tama_lcd_read(const tama_t *t, uint8_t out[TAMA_LCD_H][TAMA_LCD_W])
{
    for (int y = 0; y < TAMA_LCD_H; y++)
        for (int x = 0; x < TAMA_LCD_W; x++)
            out[y][x] = (uint8_t)tama_lcd_pixel(t, x, y);
}
