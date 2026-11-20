/* PC preview of a Nihil page: preview <book.txt> <page number> <rot> > page.pgm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/reader.h"
static uint8_t img[1920 * 1440];
int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    static uint8_t text[8 << 20];
    struct reader r = { .img = img, .pw = 1920, .ph = 1440, .rot = argc > 3 ? atoi(argv[3]) : 1, .text = text };
    r.len = fread(text, 1, sizeof text, f);
    rd_defaults(&r);
    uint32_t pos = 0, want = atoi(argv[2]), n = 1, total = 0;
    for (uint32_t p = 0; p < r.len; total++) p = rd_page(&r, p, 0);
    for (; n < want; n++) pos = rd_page(&r, pos, 0);
    memset(img, 255, sizeof img);
    uint32_t next = rd_page(&r, pos, 1);
    rd_footer(&r, n, next);
    fprintf(stderr, "pages in book: %u, this page: offsets %u..%u\n", total, pos, next);
    printf("P5\n1920 1440\n255\n");
    fwrite(img, 1, sizeof img, stdout);
    return 0;
}
