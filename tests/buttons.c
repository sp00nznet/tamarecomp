/* Do the buttons reach the ROM?
 *
 * Wiring input up is easy to get wrong in a way nothing else notices: a
 * mirrored bit mask, an inverted level, or an interrupt factor that never
 * gets raised all leave a device that boots and animates exactly as before.
 *
 * The Tamagotchi's left button opens the menu, which lights the icon row
 * along the top and bottom of the screen. So: let it settle, press left, and
 * require the screen to become something it was not.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tamarecomp/lcd.h"

typedef uint8_t frame_t[TAMA_LCD_H][TAMA_LCD_W];

/* Run for `secs`, collecting the set of distinct frames seen. Returns how
 * many of `seen` were filled. */
static int settle(tama_t *t, double secs, frame_t *seen, int max)
{
    int n = 0;
    for (int i = 0; i < (int)(secs * 8); i++) {
        tama_step(t, TAMA_OSC1_HZ / 8);
        frame_t f;
        tama_lcd_read(t, f);
        int known = 0;
        for (int j = 0; j < n; j++)
            if (memcmp(seen[j], f, sizeof f) == 0) {
                known = 1;
                break;
            }
        if (!known && n < max)
            memcpy(seen[n++], f, sizeof f);
    }
    return n;
}

int main(void)
{
    tama_t *t = calloc(1, sizeof(tama_t));
    frame_t *idle = calloc(64, sizeof(frame_t));
    if (!t || !idle)
        return 2;
    tama_reset(t);

    /* Everything the idle animation does on its own. */
    int n_idle = settle(t, 20.0, idle, 64);
    printf("idle: %d distinct frames in 20 s\n", n_idle);
    if (n_idle < 2) {
        printf("FAIL: the device is not animating; test cannot mean anything\n");
        return 1;
    }

    /* Now hold the left button for a quarter second and let it act. */
    tama_set_buttons(t, TAMA_BTN_LEFT);
    tama_step(t, TAMA_OSC1_HZ / 4);
    tama_set_buttons(t, 0);

    frame_t after[32];
    int n_after = settle(t, 3.0, after, 32);
    printf("after pressing left: %d distinct frames in 3 s\n", n_after);

    int novel = 0;
    for (int i = 0; i < n_after; i++) {
        int known = 0;
        for (int j = 0; j < n_idle; j++)
            if (memcmp(idle[j], after[i], sizeof after[i]) == 0) {
                known = 1;
                break;
            }
        if (!known)
            novel++;
    }
    printf("frames never seen while idle: %d\n", novel);

    if (t->trapped) {
        printf("FAIL: trapped at 0x%04X\n", t->pc);
        return 1;
    }
    if (novel == 0) {
        printf("FAIL: pressing a button changed nothing on screen\n");
        return 1;
    }

    /* Show what the press produced. */
    printf("\nfirst screen the button produced:\n");
    for (int i = 0; i < n_after; i++) {
        int known = 0;
        for (int j = 0; j < n_idle; j++)
            if (memcmp(idle[j], after[i], sizeof after[i]) == 0)
                known = 1;
        if (known)
            continue;
        for (int y = 0; y < TAMA_LCD_H; y++) {
            fputs("   ", stdout);
            for (int x = 0; x < TAMA_LCD_W; x++)
                putchar(after[i][y][x] ? '#' : '.');
            putchar('\n');
        }
        break;
    }
    free(idle);
    free(t);
    return 0;
}
