/*
 * Where the latest reading places are kept while Nihil runs: a fixed block of
 * (tag, value) pairs in RAM. On the Sage these are the first pairs of the
 * black box, which survives the restart back into Kobo's Linux; there
 * tools/kobo/positions.sh copies them into the positions file (see app.h).
 * Nothing in here touches storage.
 *
 *   pair 0        EF000000 = "POS1"
 *   pair 1, 2     EF000010 light, EF000011 warmth
 *   pair 3..22    4 books x 5: EF0001s0 path hash, s1 text offset, s2 page, s3 stamp, s4 check
 *   pair 23, 24   EF000300 length of the path of the book read last, EF000301 its hash
 *   pair 25..84   EF0002ii that path, 4 bytes per value, low byte first
 */
#pragma once
#include "app.h"

#define T_POS        0xEF000000u
#define POSBOX_SLOTS 4u
#define POSBOX_PAIRS (25u + POS_PATH_MAX / 4u)

static uint32_t posbox_slot, posbox_last_hash;

static void posbox_init(volatile uint32_t *p)
{
    uint32_t n = 0;
#define POSBOX_TAG(t) do { p[n * 2] = (t); p[n * 2 + 1] = 0; n++; } while (0)
    POSBOX_TAG(T_POS);
    POSBOX_TAG(T_POS | 0x10); POSBOX_TAG(T_POS | 0x11);
    for (uint32_t s = 0; s < POSBOX_SLOTS; s++)
        for (uint32_t f = 0; f < 5; f++) POSBOX_TAG(T_POS | 0x100 | s << 4 | f);
    POSBOX_TAG(T_POS | 0x300); POSBOX_TAG(T_POS | 0x301);
    for (uint32_t i = 0; i < POS_PATH_MAX / 4u; i++) POSBOX_TAG(T_POS | 0x200 | i);
#undef POSBOX_TAG
    p[1] = POS_MAGIC;
    posbox_slot = POSBOX_SLOTS - 1; posbox_last_hash = 0;
}

static void posbox_save(volatile uint32_t *p, const struct pos_rec *r, const char *path, uint32_t light, uint32_t warmth)
{
    p[1 * 2 + 1] = light; p[2 * 2 + 1] = warmth;
    if (!r) return;
    uint32_t s = POSBOX_SLOTS;
    for (uint32_t i = 0; i < POSBOX_SLOTS; i++)
        if (p[(3 + i * 5) * 2 + 1] == r->hash) s = i;
    if (s == POSBOX_SLOTS) { posbox_slot = (posbox_slot + 1) % POSBOX_SLOTS; s = posbox_slot; }
    volatile uint32_t *v = &p[(3 + s * 5) * 2];
    v[9] = 0;                                              /* check off while the record changes */
    v[1] = r->hash; v[3] = r->off; v[5] = r->page; v[7] = r->stamp;
    v[9] = pos_check(r);
    if (r->hash != posbox_last_hash) {                     /* another book: note its path as "read last" */
        uint32_t n = rd_strlen(path);
        p[23 * 2 + 1] = 0;
        if (n > 0 && n <= POS_PATH_MAX) {
            for (uint32_t i = 0; i < POS_PATH_MAX / 4u; i++) {
                uint32_t w = 0;
                for (uint32_t b = 0; b < 4; b++)
                    if (i * 4 + b < n) w |= (uint32_t)(uint8_t)path[i * 4 + b] << (8 * b);
                p[(25 + i) * 2 + 1] = w;
            }
            p[24 * 2 + 1] = r->hash;
            p[23 * 2 + 1] = n;
        }
        posbox_last_hash = r->hash;
    }
}
