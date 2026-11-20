/*
 * Frontlight of the Kobo Sage: two AW99703 LED driver chips ("FL1" on I2C bus
 * twi0, "FL2" on twi1, both at address 0x36), mixed to get cold or warm light.
 *
 * Nothing here is guessed. Two things are copied from Kobo:
 *
 *   1. WHAT to write: the sequence of register writes is the one Kobo's own
 *      kernel driver uses (drivers/leds/leds-aw99703.c: aw99703_backlight_init,
 *      aw99703_set_cur_brightness_ex, fl_aw99703_x2_percentage), with the
 *      settings from Kobo's board file (sun8iw15p1-E80P04-C2.dts: 2 channels,
 *      PWM off, ramps 0, i2c-trans-dim 7).
 *
 *   2. HOW BRIGHT: the numbers per brightness step come from the table that
 *      Kobo's bootloader leaves in RAM ("ntxfw", format in
 *      arch/arm/mach-sunxi/ntx_firmware.h). It has one sheet per colour
 *      ("T0" .. "T10" on the Sage), each with 100 brightness steps giving the
 *      LED current setting and the 11-bit brightness value for both chips.
 *      Nihil never invents a value: a brightness that is not in the table
 *      cannot be sent.
 *
 * The file that includes this one provides:
 *   int  light_rd(int chip, uint8_t reg);             -1 = chip did not answer
 *   int  light_wr(int chip, uint8_t reg, uint8_t v);   1 = ok
 *   void light_log(uint32_t tag, uint32_t v);
 */
#pragma once
#include <stdint.h>

#define AW_ID      0x00u
#define AW_MODE    0x02u
#define AW_LEDCUR  0x03u
#define AW_BSTCTR1 0x04u
#define AW_BSTCTR2 0x05u
#define AW_LEDLSB  0x06u
#define AW_LEDMSB  0x07u
#define AW_TURNCFG 0x09u
#define AW_TRANCFG 0x0Au
#define AW_CHIP_ID 0x03u

#define T_LIGHT 0xED000000u
/* 0xED0c chip c found/ID, 0xED1c.. registers of chip c as found, 0xED20 table colours,
 * 0xED21 table address, 0xED30 set (percent<<8|colour), 0xED31/32 values chip 0/1 (current<<16|brightness),
 * 0xEDE0 error */

#define NTXFW_ITEM_HDR   56u
#define NTXFW_TYPE_AW_HDR 16u
#define NTXFW_TYPE_AW_TAB 17u
#define AW_TAB_SIZE     1044u       /* one colour sheet (NTX_FW_AW99703FL_DUALFL_percent_tab) */

struct light {
    const uint8_t *tab;             /* the colour sheets in RAM, 0 = no table */
    uint32_t colors;
    uint8_t def_cur[2];             /* table-wide default current per chip */
    int chips_ok;                   /* both chips answered with the right ID */
    int on;                         /* light currently on (so: chips are set up) */
};

static uint32_t lt_u16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t lt_u32(const uint8_t *p) { return lt_u16(p) | lt_u16(p + 2) << 16; }

/* Find the AW99703 table inside Kobo's "ntxfw" block. Returns 0 when found. */
static int light_parse(struct light *l, const uint8_t *fw, uint32_t max)
{
    static const uint8_t sig[9] = { '{', 'N', 'T', 'X', 'F', 'W', '-', '-', '>' };
    static const uint8_t magic[8] = { 0xff, 0xfc, 0x5c, 0xc5, 0x5c, 0xc5, 0xcf, 0xff };
    l->tab = 0; l->colors = 0; l->def_cur[0] = l->def_cur[1] = 0;
    if (max < 52) return -1;
    for (int i = 0; i < 9; i++) if (fw[i] != sig[i]) return -1;
    uint32_t items = lt_u16(fw + 46), off = 52, hdr_colors = 0;
    for (uint32_t n = 0; n < items && n < 64; n++) {
        if (off + NTXFW_ITEM_HDR > max) return -2;
        for (int i = 0; i < 8; i++) if (fw[off + i] != magic[i]) return -3;
        uint32_t type = lt_u16(fw + off + 12), size = lt_u32(fw + off + 16);
        const uint8_t *body = fw + off + NTXFW_ITEM_HDR;
        if (size > max || off + NTXFW_ITEM_HDR + size > max) return -2;
        if (type == NTXFW_TYPE_AW_HDR && size >= 8) {
            hdr_colors = lt_u32(body);
            l->def_cur[0] = body[4]; l->def_cur[1] = body[5];
        }
        if (type == NTXFW_TYPE_AW_TAB && size >= AW_TAB_SIZE && size % AW_TAB_SIZE == 0) {
            l->tab = body;
            l->colors = size / AW_TAB_SIZE;
        }
        off += NTXFW_ITEM_HDR + size;
    }
    if (!l->tab || !l->colors || (hdr_colors && hdr_colors != l->colors)) { l->tab = 0; l->colors = 0; return -4; }
    /* every value in the table must fit the chip: brightness 11 bits, current 5 bits */
    for (uint32_t c = 0; c < l->colors; c++) {
        const uint8_t *t = l->tab + c * AW_TAB_SIZE;
        for (uint32_t p = 0; p < 100; p++)
            if (lt_u16(t + 36 + p * 2) > 2047 || lt_u16(t + 336 + p * 2) > 2047 || t[236 + p] > 31 || t[536 + p] > 31) {
                l->tab = 0; l->colors = 0;
                return -5;
            }
    }
    return 0;
}

/* The numbers Kobo would send for this brightness step (1..100) and colour sheet. */
static void light_values(const struct light *l, uint32_t percent, uint32_t color, uint8_t cur[2], uint16_t brt[2])
{
    const uint8_t *t = l->tab + color * AW_TAB_SIZE;
    static const uint16_t brt_off[2] = { 36, 336 }, cur_off[2] = { 236, 536 };
    for (int c = 0; c < 2; c++) {
        uint8_t v = t[cur_off[c] + percent - 1];
        if (!v) v = t[32 + c];                              /* this colour's default */
        if (!v) v = l->def_cur[c];                          /* the table's default */
        cur[c] = v;
        brt[c] = (uint16_t)lt_u16(t + brt_off[c] + (percent - 1) * 2);
    }
}

static int light_bits(int chip, uint8_t reg, uint8_t keep_mask, uint8_t val)   /* like aw99703_i2c_write_bit */
{
    int r = light_rd(chip, reg);
    if (r < 0) return 0;
    return light_wr(chip, reg, (uint8_t)((r & keep_mask) | val));
}

/* Look at both chips: do they answer, are they AW99703, and what did Linux leave in them. */
static int light_probe(struct light *l)
{
    l->chips_ok = 1;
    for (int c = 0; c < 2; c++) {
        int id = light_rd(c, AW_ID);
        light_log(T_LIGHT | (uint32_t)c, (uint32_t)id);
        if (id != (int)AW_CHIP_ID) { l->chips_ok = 0; continue; }
        uint32_t w = 0;
        for (uint8_t r = 2; r <= 5; r++) w |= ((uint32_t)light_rd(c, r) & 0xFFu) << ((r - 2) * 8);
        light_log(T_LIGHT | 0x10u | (uint32_t)c, w);        /* MODE | LEDCUR<<8 | BSTCTR1<<16 | BSTCTR2<<24 */
        w = 0;
        static const uint8_t more[4] = { AW_LEDLSB, AW_LEDMSB, AW_TURNCFG, AW_TRANCFG };
        for (int i = 0; i < 4; i++) w |= ((uint32_t)light_rd(c, more[i]) & 0xFFu) << (i * 8);
        light_log(T_LIGHT | 0x18u | (uint32_t)c, w);        /* LEDLSB | LEDMSB<<8 | TURNCFG<<16 | TRANCFG<<24 */
    }
    if (!l->chips_ok) light_log(T_LIGHT | 0xE0, 1);
    return l->chips_ok ? 0 : -1;
}

/* Kobo's aw99703_backlight_init(), with the Sage board file's settings. */
static int light_chip_init(int c)
{
    int ok = 1;
    ok &= light_bits(c, AW_MODE, (uint8_t)~(1u << 4), 1u << 4);       /* PWM input off */
    ok &= light_bits(c, AW_MODE, (uint8_t)~(1u << 2), 1u << 2);       /* linear brightness steps */
    ok &= light_bits(c, AW_BSTCTR1, (uint8_t)~(7u << 2), 3u << 2);    /* over-voltage limit 38 V */
    ok &= light_bits(c, AW_BSTCTR1, (uint8_t)~(1u << 5), 1u << 5);    /* switching at 1 MHz */
    ok &= light_bits(c, AW_BSTCTR1, (uint8_t)~(3u << 0), 3u << 0);    /* over-current limit 3.3 A */
    ok &= light_bits(c, AW_BSTCTR2, (uint8_t)~(1u << 6), 1u << 6);    /* 10 uH coil */
    ok &= light_bits(c, AW_LEDCUR, (uint8_t)~(31u << 3), 0u << 3);    /* full-scale current: board value 0 */
    ok &= light_bits(c, AW_LEDCUR, (uint8_t)~(7u << 0), 3u);          /* LED strings 1 and 2 on */
    ok &= light_bits(c, AW_TURNCFG, (uint8_t)~(15u << 4), 0);         /* no turn-on ramp */
    ok &= light_bits(c, AW_TURNCFG, (uint8_t)~(15u << 0), 0);         /* no turn-off ramp */
    ok &= light_bits(c, AW_TRANCFG, (uint8_t)~(7u << 4), 0);
    ok &= light_bits(c, AW_TRANCFG, (uint8_t)~(15u << 0), 7);         /* smooth step between two levels */
    return ok;
}

/* Kobo's aw99703_set_cur_brightness_ex(). */
static int light_chip_set(int c, uint8_t cur, uint16_t brt)
{
    int ok = 1;
    ok &= light_bits(c, AW_LEDCUR, (uint8_t)~(31u << 3), (uint8_t)(cur << 3));
    ok &= light_wr(c, AW_LEDLSB, (uint8_t)(brt & 7u));
    ok &= light_wr(c, AW_LEDMSB, (uint8_t)(brt >> 3));
    ok &= light_bits(c, AW_MODE, (uint8_t)~3u, brt ? 1u : 0u);       /* 1 = light mode, 0 = standby */
    return ok;
}

/* percent 0 = off, 1..100 = Kobo's brightness steps; color = sheet number 0..colors-1. 0 = ok. */
static int light_set(struct light *l, uint32_t percent, uint32_t color)
{
    if (!l->tab || !l->chips_ok) return -1;
    if (percent > 100) percent = 100;
    if (color >= l->colors) color = l->colors - 1;
    uint8_t cur[2] = { 0, 0 };
    uint16_t brt[2] = { 0, 0 };
    if (percent) light_values(l, percent, color, cur, brt);
    light_log(T_LIGHT | 0x30, percent << 8 | color);
    int ok = 1;
    for (int c = 0; c < 2; c++) {
        if (percent && !l->on) ok &= light_chip_init(c);    /* Kobo sets the chip up again each time the light comes on */
        if (!ok) break;
        ok &= light_chip_set(c, cur[c], brt[c]);
        light_log(T_LIGHT | (0x31u + (uint32_t)c), (uint32_t)cur[c] << 16 | brt[c]);
    }
    if (!ok) {
        light_log(T_LIGHT | 0xE0, 2);
        for (int c = 0; c < 2; c++) light_chip_set(c, 0, 0); /* a chip stopped answering: leave both dark */
        l->on = 0;
        return -2;
    }
    l->on = percent != 0;
    return 0;
}
