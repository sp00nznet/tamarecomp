/* Reading the Tamagotchi P1 screen out of display RAM. */
#ifndef TAMARECOMP_LCD_H
#define TAMARECOMP_LCD_H

#include "tamarecomp/e0c6200.h"

#define TAMA_LCD_W 32
#define TAMA_LCD_H 16

int  tama_lcd_pixel(const tama_t *t, int x, int y);
void tama_lcd_read(const tama_t *t, uint8_t out[TAMA_LCD_H][TAMA_LCD_W]);

/* No icon accessor: the P1's icon row is drawn inside the matrix above, not on
 * segments of its own. See the note in src/lcd.c. */

#endif
