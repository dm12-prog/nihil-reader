/*
 * READ-ONLY driver for the Kobo Sage's built-in storage chip (eMMC) on the
 * Allwinner SD/MMC controller number 2 ("sdc2", 0x04022000).
 *
 * Safety rules, enforced in mmc_start() below, not just promised:
 *   - Only five commands can leave this file: 13 (ask status), 8 (read the
 *     chip's own description), 17 and 18 (read blocks), 12 (stop reading).
 *     Any other command number is refused before it reaches the hardware.
 *   - The controller's "this is a write" bit is never set, and a command word
 *     that has it is refused.
 *   - No erase, no write, no settings change on the chip. Nothing in here can
 *     alter what is stored.
 *
 * How it gets the chip going: it does not. Nihil is started from inside Kobo's
 * Linux, which has already set the chip up (address 1, 8 data wires, fast
 * mode). This driver takes that state over as it finds it ("inherit"), checks
 * it, and reads. A from-scratch start-up is only needed once Nihil boots on its
 * own, and is not written yet.
 *
 * Sources: register layout and bit names from Kobo's kernel
 * (drivers/mmc/host/sunxi-mmc.h, sunxi-mmc-v4p5x.c) and Kobo's U-Boot
 * (drivers/mmc/sun8iw15p1/sunxi_host0_2_v4p1.[ch]), which also shows reading
 * through the FIFO without DMA (mmc_trans_data_by_cpu). Commands and the
 * EXT_CSD fields are from the JEDEC eMMC standard (JESD84-B51), as used by
 * Linux include/linux/mmc/mmc.h.
 *
 * The file that includes this one provides:
 *   uint32_t mmc_rd(uint32_t off);            controller register read
 *   void     mmc_wr(uint32_t off, uint32_t v);
 *   uint32_t mmc_ccu_rd(uint32_t off);        clock controller register read
 *   uint32_t mmc_time_us(void);               free-running microseconds
 *   void     mmc_log(uint32_t tag, uint32_t v);
 *   void     mmc_feed(void);                  keep the watchdog happy
 */
#pragma once
#include <stdint.h>

#define SMHC2_BASE     0x04022000u
#define CCU_SMHC2_CLK  0x838u          /* bit 31 = clock to the controller on  */
#define CCU_SMHC_BGR   0x84Cu          /* bit 2 = bus clock on, bit 18 = out of reset */

/* controller registers */
#define MR_GCTRL  0x00u
#define MR_CLKCR  0x04u
#define MR_TMOUT  0x08u
#define MR_WIDTH  0x0Cu
#define MR_BLKSZ  0x10u
#define MR_BCNTR  0x14u
#define MR_CMDR   0x18u
#define MR_CARG   0x1Cu
#define MR_RESP0  0x20u
#define MR_IMASK  0x30u
#define MR_RINTR  0x38u
#define MR_STAS   0x3Cu
#define MR_FTRGL  0x40u
#define MR_FUNS   0x44u
#define MR_DBGC   0x50u
#define MR_CSDC   0x54u
#define MR_NTSR   0x5Cu
#define MR_DMAC   0x80u
#define MR_IDST   0x88u
#define MR_IDIE   0x8Cu
#define MR_THLD   0x100u
#define MR_EDSD   0x10Cu
#define MR_DRV_DL 0x140u
#define MR_SAMP_DL 0x144u
#define MR_DS_DL  0x148u
#define MR_FIFO   0x200u

#define MG_SOFT_RESET  (1u << 0)
#define MG_FIFO_RESET  (1u << 1)
#define MG_DMA_RESET   (1u << 2)
#define MG_INT_ENABLE  (1u << 4)
#define MG_DMA_ENABLE  (1u << 5)
#define MG_BY_AHB      (1u << 31)     /* data goes through the FIFO register, not DMA */

#define MC_RESP        (1u << 6)
#define MC_CRC         (1u << 8)
#define MC_DATA        (1u << 9)
#define MC_WRITE       (1u << 10)     /* never used; refused */
#define MC_AUTO_STOP   (1u << 12)
#define MC_WAIT_PRE    (1u << 13)
#define MC_ABORT       (1u << 14)
#define MC_UPCLK       (1u << 21)
#define MC_START       (1u << 31)

#define MI_CMD_DONE    (1u << 2)
#define MI_DATA_OVER   (1u << 3)
#define MI_AUTO_DONE   (1u << 14)
#define MI_ERRORS      0xBFC2u        /* every error bit (Kobo U-Boot uses the same mask) */

#define MS_FIFO_EMPTY  (1u << 2)
#define MS_CARD_BUSY   (1u << 9)
#define MS_FSM_BUSY    (1u << 10)

/* black-box tags, all under 0xEC000000 */
#define T_MMC 0xEC000000u
/* 0xEC00 step, 0xEC1x clock/controller registers as found, 0xEC2x chip facts,
 * 0xEC3x read statistics, 0xECE0 error (value = code<<24 | detail), 0xECFF refused command */

enum {
    MMC_OK = 0,
    MMC_E_CLOCK = 1,      /* controller has no clock: Linux did not leave it running */
    MMC_E_BUSY = 2,       /* controller never went idle */
    MMC_E_RESET = 3,
    MMC_E_CMD = 4,        /* a command got no (good) answer */
    MMC_E_STATE = 5,      /* chip is not in "ready to transfer" state */
    MMC_E_DATA = 6,       /* data did not arrive or failed its checksum */
    MMC_E_BOOTAREA = 7,   /* chip is switched to a boot area, not the normal storage */
    MMC_E_RANGE = 8,      /* asked for blocks past the end */
    MMC_E_REFUSED = 9,    /* tried to send a command that is not on the read-only list */
    MMC_E_NOTREADY = 10,
};

static struct {
    int ready;
    uint32_t sectors;         /* size of the normal storage area in 512-byte blocks */
    int byte_addr;            /* 1 on chips of 2 GB or less: address in bytes, not blocks */
    uint32_t reads, retries, blocks;
    uint32_t last_err;
} mmc;

static void mmc_err(uint32_t code, uint32_t detail)
{
    mmc.last_err = code << 24 | (detail & 0xFFFFFFu);
    mmc_log(T_MMC | 0xE0, mmc.last_err);
}

static int mmc_wait_clear(uint32_t reg, uint32_t bits, uint32_t timeout_us)
{
    uint32_t t = mmc_time_us();
    while (mmc_rd(reg) & bits)
        if (mmc_time_us() - t > timeout_us) return -1;
    return 0;
}

/* THE gate: every command goes through here. */
static int mmc_start(uint32_t idx, uint32_t arg, uint32_t flags)
{
    uint32_t word = MC_START | flags | idx;
    int allowed = idx == 13 || idx == 8 || idx == 17 || idx == 18 || idx == 12;
    if (!allowed || (word & MC_WRITE) || idx > 63) {
        mmc_log(T_MMC | 0xFF, word);
        mmc_err(MMC_E_REFUSED, idx);
        return MMC_E_REFUSED;
    }
    mmc_wr(MR_RINTR, 0xFFFFFFFFu);
    mmc_wr(MR_CARG, arg);
    mmc_wr(MR_CMDR, word);
    return MMC_OK;
}

/* Wait until the command itself was sent and answered. */
static int mmc_wait_cmd(uint32_t idx, uint32_t *resp)
{
    uint32_t t = mmc_time_us();
    for (;;) {
        uint32_t ri = mmc_rd(MR_RINTR);
        if (ri & MI_ERRORS) { mmc_err(MMC_E_CMD, idx << 16 | (ri & 0xFFFF)); return MMC_E_CMD; }
        if (ri & MI_CMD_DONE) break;
        if (mmc_time_us() - t > 200000u) { mmc_err(MMC_E_CMD, idx << 16 | 0xFFFF); return MMC_E_CMD; }
    }
    if (resp) *resp = mmc_rd(MR_RESP0);
    return MMC_OK;
}

static int mmc_status(uint32_t *st)                       /* command 13: "how are you?" */
{
    int r = mmc_start(13, 1u << 16, MC_RESP | MC_CRC | MC_WAIT_PRE);
    if (r) return r;
    return mmc_wait_cmd(13, st);
}
#define MMC_STATE(st) (((st) >> 9) & 15u)                  /* 4 = ready to transfer, 5 = sending data */

/* Put the controller side back in order after a failed read. Chip settings are not touched. */
static int mmc_recover(void)
{
    static const uint16_t keep[] = { MR_CLKCR, MR_TMOUT, MR_WIDTH, MR_FTRGL, MR_FUNS, MR_DBGC, MR_CSDC,
                                     MR_NTSR, MR_THLD, MR_EDSD, MR_DRV_DL, MR_SAMP_DL, MR_DS_DL };
    uint32_t val[sizeof keep / sizeof keep[0]];
    for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++) val[i] = mmc_rd(keep[i]);
    mmc_wr(MR_GCTRL, MG_SOFT_RESET | MG_FIFO_RESET | MG_DMA_RESET);
    if (mmc_wait_clear(MR_GCTRL, MG_SOFT_RESET | MG_FIFO_RESET | MG_DMA_RESET, 250000u)) {
        mmc_err(MMC_E_RESET, 1);
        return MMC_E_RESET;
    }
    for (unsigned i = 0; i < sizeof keep / sizeof keep[0]; i++) mmc_wr(keep[i], val[i]);
    mmc_wr(MR_IMASK, 0);
    mmc_wr(MR_GCTRL, MG_BY_AHB);
    /* tell the controller to take the clock settings again (as Linux and U-Boot do after a reset) */
    mmc_wr(MR_CLKCR, val[0] | 1u << 31);
    mmc_wr(MR_CMDR, MC_START | MC_UPCLK | MC_WAIT_PRE);
    int bad = mmc_wait_clear(MR_CMDR, MC_START, 250000u);
    mmc_wr(MR_CLKCR, val[0] & ~(1u << 31));
    mmc_wr(MR_RINTR, 0xFFFFFFFFu);
    if (bad) { mmc_err(MMC_E_RESET, 2); return MMC_E_RESET; }

    /* the chip may still be sending the blocks we gave up on: tell it to stop, wait for "ready" */
    uint32_t t = mmc_time_us(), st = 0;
    int stopped = 0;
    for (;;) {
        if (mmc_status(&st) == MMC_OK) {
            if (MMC_STATE(st) == 4) return MMC_OK;
            if (MMC_STATE(st) == 5 && !stopped) {
                stopped = 1;
                if (mmc_start(12, 0, MC_RESP | MC_CRC | MC_ABORT) == MMC_OK) mmc_wait_cmd(12, 0);
            }
        }
        if (mmc_time_us() - t > 1000000u) break;
        mmc_feed();
    }
    mmc_err(MMC_E_STATE, st);
    return MMC_E_STATE;
}

/* One data-reading command: `count` blocks of 512 bytes into buf. */
static int mmc_data_cmd(uint32_t idx, uint32_t arg, uint32_t count, uint8_t *buf)
{
    uint32_t total = count * 512u, got = 0;
    uint32_t flags = MC_RESP | MC_CRC | MC_DATA | MC_WAIT_PRE | (count > 1 ? MC_AUTO_STOP : 0);
    mmc_wr(MR_BLKSZ, 512);
    mmc_wr(MR_BCNTR, total);
    int r = mmc_start(idx, arg, flags);
    if (r) return r;

    uint32_t t = mmc_time_us();
    while (got < total) {
        if (!(mmc_rd(MR_STAS) & MS_FIFO_EMPTY)) {
            uint32_t w = mmc_rd(MR_FIFO);
            buf[got] = (uint8_t)w; buf[got + 1] = (uint8_t)(w >> 8);
            buf[got + 2] = (uint8_t)(w >> 16); buf[got + 3] = (uint8_t)(w >> 24);
            got += 4;
            if (!(got & 0xFFFu)) t = mmc_time_us();        /* progress: restart the clock */
            continue;
        }
        uint32_t ri = mmc_rd(MR_RINTR);
        if (ri & MI_ERRORS) { mmc_err(MMC_E_DATA, idx << 16 | (ri & 0xFFFF)); return MMC_E_DATA; }
        if (mmc_time_us() - t > 1000000u) { mmc_err(MMC_E_DATA, idx << 16 | 0xFFFF); return MMC_E_DATA; }
    }
    uint32_t want = MI_CMD_DONE | MI_DATA_OVER | (count > 1 ? MI_AUTO_DONE : 0);
    t = mmc_time_us();
    for (;;) {
        uint32_t ri = mmc_rd(MR_RINTR);
        if (ri & MI_ERRORS) { mmc_err(MMC_E_DATA, idx << 16 | (ri & 0xFFFF)); return MMC_E_DATA; }
        if ((ri & want) == want) break;
        if (mmc_time_us() - t > 1000000u) { mmc_err(MMC_E_DATA, idx << 16 | 0xFFFE); return MMC_E_DATA; }
    }
    mmc_wr(MR_RINTR, 0xFFFFFFFFu);
    return MMC_OK;
}

/* Read `count` 512-byte blocks starting at block `lba` of the normal storage area. 0 = ok. */
static int mmc_read(uint32_t lba, uint32_t count, uint8_t *buf)
{
    if (!mmc.ready) return MMC_E_NOTREADY;
    if (lba >= mmc.sectors || count > mmc.sectors - lba) { mmc_err(MMC_E_RANGE, lba); return MMC_E_RANGE; }
    mmc.reads++;
    while (count) {
        uint32_t n = count > 256u ? 256u : count;          /* at most 128 KB per command */
        int r = MMC_OK;
        for (int attempt = 0; attempt < 4; attempt++) {
            if (attempt == 3) n = 1;                        /* last try: one block at a time */
            uint32_t arg = mmc.byte_addr ? lba * 512u : lba;
            r = mmc_data_cmd(n > 1 ? 18 : 17, arg, n, buf);
            if (r == MMC_OK) break;
            if (r == MMC_E_REFUSED) return r;
            mmc.retries++;
            if (mmc_recover()) return r;
        }
        if (r) return r;
        lba += n; count -= n; buf += n * 512u; mmc.blocks += n;
        mmc_feed();
    }
    return MMC_OK;
}

/* Take over the chip as Linux left it, and check everything we rely on. 0 = ok. */
static int mmc_attach(void)
{
    static uint8_t ext[512];
    mmc.ready = 0;
    uint32_t clk = mmc_ccu_rd(CCU_SMHC2_CLK), bgr = mmc_ccu_rd(CCU_SMHC_BGR);
    mmc_log(T_MMC | 0x10, clk);
    mmc_log(T_MMC | 0x11, bgr);
    if (!(clk & 1u << 31) || !(bgr & 1u << 2) || !(bgr & 1u << 18)) { mmc_err(MMC_E_CLOCK, 0); return MMC_E_CLOCK; }
    mmc_log(T_MMC | 0x12, mmc_rd(MR_GCTRL));
    mmc_log(T_MMC | 0x13, mmc_rd(MR_CLKCR));
    mmc_log(T_MMC | 0x14, mmc_rd(MR_WIDTH));
    mmc_log(T_MMC | 0x15, mmc_rd(MR_TMOUT));
    mmc_log(T_MMC | 0x16, mmc_rd(MR_STAS));
    mmc_log(T_MMC | 0x17, mmc_rd(MR_NTSR));
    mmc_log(T_MMC | 0x18, mmc_rd(MR_DRV_DL));
    mmc_log(T_MMC | 0x19, mmc_rd(MR_SAMP_DL));
    mmc_log(T_MMC | 0x1A, mmc_rd(MR_THLD));
    mmc_log(T_MMC | 0x1B, mmc_rd(MR_RINTR));
    mmc_log(T_MMC | 0x1C, mmc_rd(MR_DMAC));

    /* let whatever Linux had in flight finish */
    if (mmc_wait_clear(MR_CMDR, MC_START, 1000000u) || mmc_wait_clear(MR_STAS, MS_FSM_BUSY | MS_CARD_BUSY, 1000000u)) {
        mmc_err(MMC_E_BUSY, mmc_rd(MR_STAS));
        return MMC_E_BUSY;
    }
    /* our way of working: no interrupts, no DMA, data through the FIFO */
    mmc_wr(MR_IMASK, 0);
    mmc_wr(MR_IDIE, 0);
    mmc_wr(MR_DMAC, 0);
    mmc_wr(MR_GCTRL, (mmc_rd(MR_GCTRL) & ~(MG_INT_ENABLE | MG_DMA_ENABLE)) | MG_BY_AHB | MG_FIFO_RESET | MG_DMA_RESET);
    if (mmc_wait_clear(MR_GCTRL, MG_FIFO_RESET | MG_DMA_RESET, 250000u)) { mmc_err(MMC_E_RESET, 0); return MMC_E_RESET; }
    mmc_wr(MR_IDST, 0x337u);
    mmc_wr(MR_RINTR, 0xFFFFFFFFu);
    mmc_log(T_MMC, 1);

    uint32_t st = 0;
    int r = mmc_status(&st);
    mmc_log(T_MMC | 0x20, st);
    if (r) return r;
    if (MMC_STATE(st) != 4) { mmc_err(MMC_E_STATE, st); return MMC_E_STATE; }
    mmc_log(T_MMC, 2);

    r = mmc_data_cmd(8, 0, 1, ext);                        /* the chip's description of itself */
    if (r) { mmc_recover(); return r; }
    uint32_t sec = ext[212] | (uint32_t)ext[213] << 8 | (uint32_t)ext[214] << 16 | (uint32_t)ext[215] << 24;
    mmc_log(T_MMC | 0x21, sec);                            /* size in blocks */
    mmc_log(T_MMC | 0x22, ext[179]);                       /* which area is selected (low 3 bits, 0 = normal) */
    mmc_log(T_MMC | 0x23, (uint32_t)ext[183] | (uint32_t)ext[185] << 8 | (uint32_t)ext[192] << 16 | (uint32_t)ext[33] << 24);
                                                           /* data wires | speed mode<<8 | version<<16 | write cache<<24 */
    if (ext[179] & 7u) { mmc_err(MMC_E_BOOTAREA, ext[179]); return MMC_E_BOOTAREA; }
    if (!sec) { mmc_err(MMC_E_DATA, 0x080000); return MMC_E_DATA; }
    mmc.sectors = sec;
    mmc.byte_addr = sec <= 4194304u;                       /* 2 GB or less */
    mmc.ready = 1;
    mmc_log(T_MMC, 3);
    return MMC_OK;
}

static inline void mmc_log_stats(void)
{
    mmc_log(T_MMC | 0x30, mmc.reads);
    mmc_log(T_MMC | 0x31, mmc.blocks);
    mmc_log(T_MMC | 0x32, mmc.retries);
}
