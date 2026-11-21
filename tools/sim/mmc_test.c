/*
 * PC test for the Sage storage driver (platform/sage/mmc.h).
 *
 * The real controller and chip are replaced by a small stand-in written from
 * the same register descriptions. It serves blocks from a disk image file and
 * watches every command the driver sends: anything that is not on the
 * read-only list, or has the "write" bit, makes the test fail.
 *
 * This checks the driver's logic (order of steps, FIFO reading, retries,
 * refusing bad situations). It cannot prove the real chip behaves like the
 * stand-in: that needs the Kobo.
 *
 *   cc -O2 -Wall -Wextra tools/sim/mmc_test.c -o build/mmc_test
 *   build/mmc_test disk.img "/Folder/Book.epub" original-book.epub
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---------- the stand-in controller + chip ---------- */
static FILE *img;
static uint32_t img_sectors;
static uint32_t regs[0x400 / 4];
static uint8_t fifo[256 * 512];
static uint32_t fifo_len, fifo_pos;
static uint32_t fake_time;
static uint32_t card_state = 4;          /* 4 = ready to transfer */
static uint8_t ext_csd[512];
static uint32_t ccu_clk = 1u << 31, ccu_bgr = 1u << 2 | 1u << 18;
static int cmd_count[64], forbidden_seen, data_cmds, fail_at = -1, fail_always, stuck_after_fail;
static int pending_err;
static int status_fail_once, ext_fail_once;
static int quiet;

#define R(off) regs[(off) / 4]

static uint32_t mmc_time_us(void) { return fake_time += 3; }
static void mmc_feed(void) {}
static void mmc_log(uint32_t tag, uint32_t v) { if (!quiet) printf("    log %08x = %08x\n", tag, v); }
static uint32_t mmc_ccu_rd(uint32_t off) { return off == 0x838 ? ccu_clk : off == 0x84C ? ccu_bgr : 0; }

static uint32_t mmc_rd(uint32_t off)
{
    if (off == 0x200) {                                    /* FIFO */
        uint32_t w = 0;
        if (fifo_pos < fifo_len) { memcpy(&w, fifo + fifo_pos, 4); fifo_pos += 4; }
        if (fifo_pos >= fifo_len && fifo_len) {
            fifo_len = fifo_pos = 0;
            if (pending_err) { R(0x38) |= 1u << 7; pending_err = 0; }        /* data checksum error */
            else R(0x38) |= 1u << 3 | (R(0x18) & 1u << 12 ? 1u << 14 : 0);   /* data over (+ auto stop done) */
        }
        return w;
    }
    if (off == 0x3C) return (fifo_pos < fifo_len ? 0 : 1u << 2) | (R(0x3C) & ~(1u << 2));
    return R(off);
}

static void model_command(uint32_t v)
{
    uint32_t idx = v & 63;
    if (v & 1u << 21) return;                              /* clock update only */
    cmd_count[idx]++;
    if (v & 1u << 10 || !(idx == 13 || idx == 8 || idx == 17 || idx == 18 || idx == 12)) {
        forbidden_seen++;
        printf("  !!! stand-in saw a forbidden command: %u (word %08x)\n", idx, v);
        return;
    }
    if (idx == 13 && status_fail_once) { status_fail_once = 0; R(0x38) |= 1u << 8; return; }   /* no answer, once */
    if (idx == 13) { R(0x20) = card_state << 9 | 0x100; R(0x38) |= 1u << 2; return; }
    if (idx == 12) { card_state = 4; R(0x20) = 4u << 9 | 0x100; R(0x38) |= 1u << 2; return; }
    if (card_state != 4) { R(0x38) |= 1u << 8; return; }  /* no answer */
    uint32_t bytes = R(0x14);
    R(0x20) = 4u << 9 | 0x100;
    R(0x38) |= 1u << 2;
    fifo_pos = 0;
    int this_cmd = data_cmds++;
    if (idx == 8) {
        memcpy(fifo, ext_csd, 512); fifo_len = 512;
        if (ext_fail_once) { ext_fail_once = 0; fifo[100] ^= 0xFF; pending_err = 1; }
        return;
    }
    uint32_t lba = R(0x1C);
    if (img_sectors <= 4194304u) {                         /* small chips are addressed in bytes */
        if (lba % 512) { R(0x38) |= 1u << 9; return; }
        lba /= 512;
    }
    if (lba + bytes / 512 > img_sectors) { R(0x38) |= 1u << 9; return; }     /* out of range: data timeout */
    fseek(img, (long)lba * 512, SEEK_SET);
    if (fread(fifo, 1, bytes, img) != bytes) { R(0x38) |= 1u << 9; return; }
    fifo_len = bytes;
    if (fail_always || this_cmd == fail_at) {
        fifo_len = bytes > 1024 ? 1024 : 512;              /* some data arrives, then a checksum error */
        fifo[100] ^= 0xFF;
        pending_err = 1;
        if (idx == 18 && stuck_after_fail) card_state = 5; /* chip keeps sending until told to stop */
    }
}

static void mmc_wr(uint32_t off, uint32_t v)
{
    if (off == 0x38 || off == 0x88) { R(off) &= ~v; return; }               /* write 1 to clear */
    if (off == 0x00) {
        if (v & 7u) { fifo_len = fifo_pos = 0; pending_err = 0; }
        R(off) = v & ~7u;                                  /* reset bits clear themselves */
        return;
    }
    if (off == 0x18) { R(off) = v & ~(1u << 31); if (v & 1u << 31) model_command(v); return; }
    R(off) = v;
}

/* ---------- the code under test ---------- */
#include "../../platform/sage/mmc.h"
#include "../../core/fat.h"
static int hal_blk_read(uint32_t lba, uint32_t count, uint8_t *buf) { return mmc_read(lba, count, buf); }

static int fails;
static void check(const char *what, int ok) { printf("  %-62s %s\n", what, ok ? "ok" : "FAILED"); if (!ok) fails++; }

static void model_reset(void)
{
    memset(regs, 0, sizeof regs);
    R(0x00) = 1u << 4 | 1u << 5;                           /* as Linux leaves it: interrupts + DMA on */
    /* the values found on the real Sage on 21 Nov 2026 (black-box tags EC13..EC1C); note status
     * 0x9506: "data state machine busy" (bit 10) stays set although nothing is going on */
    R(0x04) = 0x30000; R(0x0C) = 2; R(0x08) = 0xFFFFFFFF; R(0x3C) = 0x9502; R(0x5C) = 0;
    R(0x140) = 0x10000; R(0x144) = 0x585; R(0x100) = 0x02000001; R(0x80) = 0x300;
    status_fail_once = ext_fail_once = 0;
    fifo_len = fifo_pos = 0; card_state = 4; pending_err = 0;
    ccu_clk = 1u << 31; ccu_bgr = 1u << 2 | 1u << 18;
    memset(ext_csd, 0, sizeof ext_csd);
    memcpy(ext_csd + 212, &img_sectors, 4);
    ext_csd[183] = 2; ext_csd[185] = 1; ext_csd[192] = 8; ext_csd[33] = 1;
    data_cmds = 0; fail_at = -1; fail_always = 0; stuck_after_fail = 0;
    memset(&mmc, 0, sizeof mmc);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: mmc_test disk.img /path/in/image original-file\n"); return 2; }
    img = fopen(argv[1], "rb");
    if (!img) { perror(argv[1]); return 2; }
    fseek(img, 0, SEEK_END);
    img_sectors = (uint32_t)(ftell(img) / 512);
    FILE *o = fopen(argv[3], "rb");
    if (!o) { perror(argv[3]); return 2; }
    static uint8_t want[32 << 20], got[32 << 20];
    uint32_t want_len = (uint32_t)fread(want, 1, sizeof want, o), got_len = 0;
    fclose(o);
    static struct fat f;
    static struct fat_entry e;

    printf("1. normal run\n");
    model_reset();
    check("take over the chip as Linux left it", mmc_attach() == MMC_OK);
    check("size read from the chip matches the image", mmc.sectors == img_sectors);
    check("book volume found", fat_mount(&f) == 0);
    check("book found by its path", fat_find(&f, argv[2], &e) == 0);
    check("book read", fat_read(&f, &e, got, sizeof got, &got_len) == 0);
    check("every byte equals the original file", got_len == want_len && !memcmp(got, want, want_len));
    printf("    %u read calls, %u blocks, %u retries; commands sent: 13 x%d, 8 x%d, 17 x%d, 18 x%d, 12 x%d\n",
           mmc.reads, mmc.blocks, mmc.retries, cmd_count[13], cmd_count[8], cmd_count[17], cmd_count[18], cmd_count[12]);
    quiet = 1;

    printf("2. one read arrives damaged (checksum error), chip keeps sending\n");
    model_reset(); mmc_attach(); fat_mount(&f);
    fail_at = data_cmds + 6; stuck_after_fail = 1;
    memset(got, 0, want_len); got_len = 0;
    check("book still read", fat_find(&f, argv[2], &e) == 0 && fat_read(&f, &e, got, sizeof got, &got_len) == 0);
    check("every byte equals the original file", got_len == want_len && !memcmp(got, want, want_len));
    check("exactly one retry was needed", mmc.retries == 1);

    printf("3. every read arrives damaged\n");
    model_reset(); mmc_attach();
    fail_always = 1;
    check("read gives up with an error instead of hanging", mmc_read(0, 8, got) == MMC_E_DATA);
    check("it tried 4 times", mmc.retries == 4);

    printf("4. situations where the driver must refuse to start\n");
    model_reset(); ccu_clk = 0;
    check("controller clock off -> refuse", mmc_attach() == MMC_E_CLOCK && !mmc.ready);
    model_reset(); card_state = 3;
    check("chip not in transfer state -> refuse", mmc_attach() == MMC_E_STATE && !mmc.ready);
    model_reset(); ext_csd[179] = 0x49;
    check("chip switched to a boot area -> refuse", mmc_attach() == MMC_E_BOOTAREA && !mmc.ready);
    check("reading while not started -> refuse", mmc_read(0, 1, got) == MMC_E_NOTREADY);
    model_reset(); mmc_attach();
    check("reading past the end -> refuse", mmc_read(img_sectors - 1, 2, got) == MMC_E_RANGE);
    check("reading the very last block works", mmc_read(img_sectors - 1, 1, got) == MMC_OK);

    printf("5. what the real Sage showed: controller says 'busy' although idle\n");
    model_reset();
    check("status as on the real device (0x9506)", mmc_rd(0x3C) == 0x9506);
    check("driver starts anyway (Kobo's own drivers ignore that bit)", mmc_attach() == MMC_OK && mmc.ready);
    model_reset(); R(0x3C) |= 1u << 9;
    check("chip's own busy line stuck -> still refuse", mmc_attach() == MMC_E_BUSY && !mmc.ready);
    model_reset(); status_fail_once = 1;
    check("first question unanswered -> controller reset, then ok", mmc_attach() == MMC_OK && mmc.ready);
    model_reset(); ext_fail_once = 1;
    check("first description read damaged -> second try ok", mmc_attach() == MMC_OK && mmc.sectors == img_sectors);
    model_reset(); mmc_attach(); fat_mount(&f);
    memset(got, 0, want_len); got_len = 0;
    check("book read with the real device's register values", fat_find(&f, argv[2], &e) == 0 &&
          fat_read(&f, &e, got, sizeof got, &got_len) == 0 && got_len == want_len && !memcmp(got, want, want_len));

    printf("6. the read-only gate\n");
    model_reset(); mmc_attach();
    int before = 0, after = 0, refused = 0;
    for (int i = 0; i < 64; i++) before += cmd_count[i];
    static const int bad[] = { 0, 1, 5, 6, 23, 24, 25, 26, 27, 35, 36, 38, 49, 62 };   /* reset, switch, write, erase, ... */
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        refused += mmc_start((uint32_t)bad[i], 0, MC_RESP | MC_CRC) == MMC_E_REFUSED;
    refused += mmc_start(17, 0, MC_RESP | MC_DATA | MC_WRITE) == MMC_E_REFUSED;       /* allowed number, write bit */
    for (int i = 0; i < 64; i++) after += cmd_count[i];
    check("15 forbidden commands refused", refused == 15);
    check("none of them reached the hardware", before == after);
    check("no forbidden command seen in the whole test", forbidden_seen == 0);

    printf(fails ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
