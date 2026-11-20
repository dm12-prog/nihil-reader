/*
 * Nihil simulator: runs the reader on a PC and writes what the screen would
 * show after each button press.
 *
 *   sim <book.txt> <keys> <out prefix> [rot]
 *     keys: a string of n (next page) and b (back), e.g. "nnnb"
 *     writes <prefix>-000.pgm (first screen), <prefix>-001.pgm (after key 1) ...
 *     Pictures are written upright (the way the reader holds the device).
 *
 * It simulates the screen's size and the two buttons. It does NOT simulate
 * the Sage's hardware (e-ink waveforms, power chip, storage).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/app.h"

#define PW 1920
#define PH 1440
static uint8_t img[PW * PH], text[8 << 20];
static uint32_t pages[1 << 20];

static void save(const struct app *a, const char *prefix, int n)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%03d.pgm", prefix, n);
    FILE *f = fopen(name, "wb");
    if (!f) { perror(name); exit(1); }
    uint32_t w = rd_page_w(&a->rd), h = rd_page_h(&a->rd);
    fprintf(f, "P5\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t X, Y;
            switch (a->rd.rot) {
            case 1:  X = PW - 1 - y; Y = x;          break;
            case 2:  X = PW - 1 - x; Y = PH - 1 - y; break;
            case 3:  X = y;          Y = PH - 1 - x; break;
            default: X = x;          Y = y;          break;
            }
            fputc(img[Y * PW + X], f);
        }
    fclose(f);
    printf("%s: page %u, text %u..%u of %u\n", name, a->page + 1, a->page_start[a->page], a->next_pos, a->rd.len);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: sim <book.txt> <keys> <out prefix> [rot]\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    uint32_t len = (uint32_t)fread(text, 1, sizeof text, f);
    fclose(f);
    struct app a;
    app_init(&a, text, len, PW, PH, argc > 4 ? (uint32_t)atoi(argv[4]) : 1, pages, 1 << 20);
    memset(img, 255, sizeof img);
    app_paint(&a, img);
    save(&a, argv[3], 0);
    int n = 1;
    for (const char *k = argv[2]; *k; k++, n++) {
        if (app_key(&a, *k == 'b' ? KEY_BACK : KEY_NEXT)) {
            memset(img, 255, sizeof img);
            app_paint(&a, img);
        }
        save(&a, argv[3], n);
    }
    return 0;
}
