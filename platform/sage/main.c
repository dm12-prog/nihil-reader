/*
 * Milestone 2, test 2a: "look, don't touch" probe of the screen hardware.
 *
 * Reads (never writes) the registers that matter for the e-ink screen and
 * stores them as (address, value) pairs in the black box, so we learn in what
 * state Linux hands the hardware over: clocks, EINK engine, timing controller,
 * IOMMU and every pin setting. Then reboots at once.
 *
 * Safety: the watchdog is armed for 16 s before the first read. If a read of
 * a switched-off block ever hangs the CPU, the Kobo still reboots by itself,
 * and bb[6] tells us which address was being read.
 */
#include "sage.h"

static uint64_t ticks(void)
{
    uint32_t lo, hi;
    __asm__ volatile("isb; mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t ticks_per_second(void)
{
    uint32_t f;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(f));
    return f ? f : 24000000u;
}

static uint64_t ms_to_ticks(uint32_t ms)
{
    return (uint64_t)ticks_per_second() / 1000u * ms;
}

static void sleep_ms(uint32_t ms)
{
    uint64_t end = ticks() + ms_to_ticks(ms);
    while (ticks() < end)
        ;
}

__attribute__((unused)) static uint32_t port_l_reg(uint32_t offset) { return R_PIO_BASE + PORT_L * 0x24u + offset; }
__attribute__((unused)) static uint32_t port_h_reg(uint32_t offset) { return PIO_BASE + PORT_H * 0x24u + offset; }

__attribute__((unused)) static void port_l_mode(int pin, uint32_t mode)
{
    uint32_t reg = port_l_reg((uint32_t)(pin / 8) * 4u);
    uint32_t shift = (uint32_t)(pin % 8) * 4u;
    REG32(reg) = (REG32(reg) & ~(0xFu << shift)) | (mode << shift);
}

__attribute__((unused)) static void port_l_write(int pin, int value)
{
    if (value)
        REG32(port_l_reg(0x10)) |= (1u << pin);
    else
        REG32(port_l_reg(0x10)) &= ~(1u << pin);
}

__attribute__((noreturn)) static void reboot(void)
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;
    REG32(WDT_MODE) = (REG32(WDT_MODE) & ~(0xFu << 4)) | 1u;
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);
    for (;;) {
        sleep_ms(5);
        REG32(WDT_MODE) |= 1u;
    }
}

static volatile uint32_t *const bb = (volatile uint32_t *)BLACKBOX_PHYS;
static uint32_t npairs;

static void rd(uint32_t addr)
{
    bb[6] = addr;                            /* progress marker: "about to read this" */
    uint32_t v = REG32(addr);
    if (npairs < BLACKBOX_MAX_PAIRS) {
        bb[BLACKBOX_EVENTS + npairs * 2] = addr;
        bb[BLACKBOX_EVENTS + npairs * 2 + 1] = v;
        npairs++;
        bb[3] = npairs;
    }
}

static void rd_range(uint32_t base, uint32_t from, uint32_t to)
{
    for (uint32_t o = from; o <= to; o += 4)
        rd(base + o);
}

static int clocked(uint32_t gate_reg, uint32_t bit)
{
    uint32_t v = REG32(gate_reg);
    return (v & (1u << bit)) && (v & (1u << (16 + bit)));   /* clock on AND out of reset */
}

static void watchdog_arm_16s(void)
{
    REG32(WDT_CFG) = (REG32(WDT_CFG) & ~0x3u) | 0x1u;       /* whole-system reset */
    REG32(WDT_MODE) = (0xBu << 4) | 1u;                     /* 16 s, enabled */
    REG32(WDT_CTRL) = (1u << 0) | (0x0A57u << 1);           /* restart the count */
}

__attribute__((noreturn)) int main(void)
{
    for (int i = 0; i < BLACKBOX_WORDS; i++)
        bb[i] = 0;
    bb[1] = 3;                               /* log format 3: (address, value) pairs */
    bb[2] = ticks_per_second();
    bb[0] = BLACKBOX_MAGIC;
    watchdog_arm_16s();

    /* 1. Clock controller: always safe to read. */
    static const uint16_t ccu[] = { 0x020, 0x040, 0x048, 0x060, 0x540, 0x600, 0x60C, 0x610,
                                    0x61C, 0x630, 0x63C, 0x640, 0x7BC, 0x804, 0xB5C, 0xB60, 0xB7C };
    for (unsigned i = 0; i < sizeof ccu / sizeof ccu[0]; i++)
        rd(CCU_BASE + ccu[i]);

    /* 2. Every pin bank A..I: function, data, drive, pull. Plus port L. */
    for (uint32_t p = 0; p < 9; p++)
        rd_range(PIO_BASE + p * 0x24u, 0x00, 0x20);
    rd_range(R_PIO_BASE, 0x00, 0x20);

    /* 3. Blocks that are only readable while their clock runs. */
    bb[7] = (uint32_t)clocked(CCU_BASE + 0x61C, 0)
          | (uint32_t)clocked(CCU_BASE + 0xB7C, 0) << 1
          | (uint32_t)clocked(CCU_BASE + 0x7BC, 0) << 2
          | (uint32_t)clocked(CCU_BASE + 0xB5C, 0) << 3
          | (uint32_t)clocked(CCU_BASE + 0x60C, 0) << 4;
    if (bb[7] & 1) {                         /* EINK engine */
        rd_range(EINK_BASE, 0x000, 0x05C);
        rd_range(EINK_BASE, 0x150, 0x15C);
        rd_range(EINK_BASE, 0x400, 0x41C);
        rd_range(EINK_BASE, 0x440, 0x444);
    }
    if (bb[7] & 2)                           /* timing controller */
        rd_range(TCON_BASE, 0x000, 0x0FC);
    if (bb[7] & 4)                           /* IOMMU */
        rd_range(IOMMU_BASE, 0x000, 0x09C);

    bb[4] = 0x600D0000u | (npairs & 0xFFFF);
    reboot();
}
