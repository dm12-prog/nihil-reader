/*
 * PC test for the Sage frontlight code (platform/sage/light.h).
 *
 * Input: a real "ntxfw" table file from Kobo's published source package
 * (tools/pack/chips/sun8iw15p1/configs/ntx_boards/ntxfw-E80P04-B1.bin). The two
 * light chips are replaced by two small register arrays.
 *
 *   cc -O2 -Wall -Wextra tools/sim/light_test.c -o build/light_test
 *   build/light_test ntxfw-E80P04-B1.bin
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint8_t chip[2][256];
static int dead[2], writes;
static int light_rd(int c, uint8_t reg) { return dead[c] ? -1 : chip[c][reg]; }
static int light_wr(int c, uint8_t reg, uint8_t v) { if (dead[c]) return 0; chip[c][reg] = v; writes++; return 1; }
static void light_log(uint32_t tag, uint32_t v) { (void)tag; (void)v; }

#include "../../platform/sage/light.h"

static int fails;
static void check(const char *what, int ok) { printf("  %-66s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }
static void chips_reset(void) { memset(chip, 0, sizeof chip); chip[0][0] = chip[1][0] = 3; dead[0] = dead[1] = 0; writes = 0; }
static uint32_t brt_of(int c) { return (uint32_t)chip[c][AW_LEDMSB] << 3 | (chip[c][AW_LEDLSB] & 7u); }

int main(int argc, char **argv)
{
    static uint8_t fw[1 << 16], bad[1 << 16];
    if (argc < 2) { fprintf(stderr, "usage: light_test ntxfw-file\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    uint32_t n = (uint32_t)fread(fw, 1, sizeof fw, f);
    fclose(f);
    struct light l;
    memset(&l, 0, sizeof l);

    printf("1. Kobo's table\n");
    check("table found in the file", light_parse(&l, fw, n) == 0);
    printf("    %u colour sheets\n", l.colors);
    for (uint32_t c = 0; c < l.colors; c++) {
        uint8_t cur[2]; uint16_t brt[2];
        light_values(&l, 100, c, cur, brt);
        printf("    sheet %-3.8s at 100%%: chip FL1 current %u brightness %4u | chip FL2 current %u brightness %4u\n",
               (const char *)(l.tab + c * AW_TAB_SIZE), cur[0], brt[0], cur[1], brt[1]);
    }
    check("11 colour sheets (T0..T10), as in the Sage B1 file", l.colors == 11);

    printf("2. switching on: 50%%, sheet 5\n");
    chips_reset();
    check("both chips answer with the AW99703 ID", light_probe(&l) == 0);
    uint8_t cur[2]; uint16_t brt[2];
    light_values(&l, 50, 5, cur, brt);
    check("light_set ok", light_set(&l, 50, 5) == 0);
    for (int c = 0; c < 2; c++) {
        char w[80];
        snprintf(w, sizeof w, "chip %d: brightness registers hold the table value (%u)", c, brt[c]);
        check(w, brt_of(c) == brt[c]);
        snprintf(w, sizeof w, "chip %d: current setting = table value (%u), strings 1+2 on", c, cur[c]);
        check(w, chip[c][AW_LEDCUR] == (uint8_t)(cur[c] << 3 | 3));
        check("        set-up as Kobo's driver: MODE 0x15, BSTCTR1 0x2F, BSTCTR2 0x40, TRANCFG 0x07",
              chip[c][AW_MODE] == 0x15 && chip[c][AW_BSTCTR1] == 0x2F && chip[c][AW_BSTCTR2] == 0x40 &&
              chip[c][AW_TRANCFG] == 0x07 && chip[c][AW_TURNCFG] == 0);
    }

    printf("3. changing level while on, then off\n");
    int w0 = writes;
    light_values(&l, 100, 10, cur, brt);
    check("light_set 100%, sheet 10", light_set(&l, 100, 10) == 0 && brt_of(0) == brt[0] && brt_of(1) == brt[1]);
    check("no second set-up while the light stays on (4 writes per chip)", writes - w0 == 8);
    check("a chip with brightness 0 in the table goes to standby", (chip[1][AW_MODE] & 3) == (brt[1] ? 1 : 0));
    check("off", light_set(&l, 0, 0) == 0 && brt_of(0) == 0 && brt_of(1) == 0 &&
                 (chip[0][AW_MODE] & 3) == 0 && (chip[1][AW_MODE] & 3) == 0 && !l.on);

    printf("4. out-of-range requests are clamped to the table\n");
    light_values(&l, 100, 10, cur, brt);
    check("percent 250, sheet 99 -> 100%, last sheet", light_set(&l, 250, 99) == 0 && brt_of(0) == brt[0] && brt_of(1) == brt[1]);
    uint32_t top = 0;
    for (uint32_t c = 0; c < l.colors; c++)
        for (uint32_t p = 1; p <= 100; p++) { light_values(&l, p, c, cur, brt); if (brt[0] > top) top = brt[0]; if (brt[1] > top) top = brt[1]; }
    printf("    highest brightness value anywhere in the table: %u (chip limit 2047)\n", top);
    check("no table value exceeds the chip's range", top <= 2047);

    printf("5. things that must be refused\n");
    chips_reset(); dead[1] = 1; l.on = 0;
    check("one chip silent -> probe fails", light_probe(&l) != 0);
    check("                -> light_set refuses", light_set(&l, 50, 5) != 0);
    chips_reset(); light_probe(&l); l.on = 0;
    light_set(&l, 50, 5); dead[1] = 1;
    check("chip stops answering mid-way -> error, other chip left dark", light_set(&l, 80, 5) != 0 && brt_of(0) == 0);
    memcpy(bad, fw, n);
    bad[0] = 'X';
    struct light l2;
    check("block without Kobo's signature -> no table", light_parse(&l2, bad, n) != 0 && !l2.tab);
    memcpy(bad, fw, n);
    uint32_t tab_off = (uint32_t)(l.tab - fw);
    bad[tab_off + 36 + 2 * 60 + 1] = 0x09;                  /* a brightness value above 2047 */
    check("table with a value beyond the chip's range -> refused", light_parse(&l2, bad, n) == -5 && !l2.tab);
    check("truncated block -> refused", light_parse(&l2, fw, 4000) != 0 && !l2.tab);
    check("no table -> light_set refuses", light_set(&l2, 50, 5) != 0);

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
