/* Dump every distinct LCD frame the ROM produces, with the second it appeared.
 *
 * A static picture proves the LCD mapping; a changing one proves the clock
 * timer interrupt is actually driving the device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamarecomp/lcd.h"

int main(int argc, char **argv)
{
    int secs = (argc > 1) ? atoi(argv[1]) : 30;
    tama_t *t = calloc(1, sizeof(tama_t));
    if (!t)
        return 2;
    tama_reset(t);

    uint8_t px[TAMA_LCD_H][TAMA_LCD_W], prev[TAMA_LCD_H][TAMA_LCD_W];
    memset(prev, 0xFF, sizeof prev);
    int frames = 0;

    for (int i = 0; i < secs * 8; i++) {
        tama_step(t, TAMA_OSC1_HZ / 8);
        if (t->trapped) {
            printf("trapped at 0x%04X\n", t->pc);
            return 1;
        }
        tama_lcd_read(t, px);
        if (memcmp(px, prev, sizeof px) == 0)
            continue;

        frames++;
        printf("\n--- frame %d at %.2fs ---\n", frames,
               (double)t->cycles / TAMA_OSC1_HZ);
        for (int y = 0; y < TAMA_LCD_H; y++) {
            fputs("   ", stdout);
            for (int x = 0; x < TAMA_LCD_W; x++)
                putchar(px[y][x] ? '#' : '.');
            putchar('\n');
        }
        memcpy(prev, px, sizeof px);
    }

    printf("\n%d distinct frames in %d s\n", frames, secs);
    free(t);
    return frames > 1 ? 0 : 1;
}
