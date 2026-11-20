/*
 * ZIP reader for a file that is already in memory (an EPUB is a ZIP file).
 * Layout per PKWARE's APPNOTE.TXT: end record -> central directory -> entries.
 * Supports "stored" (0) and "deflate" (8) entries. No ZIP64, no encryption.
 */
#pragma once
#include <stdint.h>
#include "inflate.h"

struct zip { const uint8_t *d; uint32_t len, cd_off, cd_count; };
struct zip_entry { const uint8_t *name; uint32_t name_len, method, csize, usize, data_off; };

static uint32_t zip_u16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t zip_u32(const uint8_t *p) { return zip_u16(p) | zip_u16(p + 2) << 16; }

static int zip_open(struct zip *z, const uint8_t *d, uint32_t len)
{
    z->d = d; z->len = len;
    if (len < 22) return -1;
    for (uint32_t i = len - 22, n = 0; n < 0x10000u + 22u; n++, i--) {
        if (zip_u32(d + i) == 0x06054b50u) {
            z->cd_count = zip_u16(d + i + 10);
            z->cd_off = zip_u32(d + i + 16);
            return z->cd_off < len ? 0 : -1;
        }
        if (i == 0) break;
    }
    return -1;
}

/* Finds an entry by its exact name. Returns 0 when found. */
static int zip_find(const struct zip *z, const char *name, uint32_t name_len, struct zip_entry *e)
{
    uint32_t p = z->cd_off;
    for (uint32_t i = 0; i < z->cd_count; i++) {
        if (p + 46 > z->len || zip_u32(z->d + p) != 0x02014b50u) return -1;
        uint32_t nl = zip_u16(z->d + p + 28), xl = zip_u16(z->d + p + 30), cl = zip_u16(z->d + p + 32);
        if (p + 46 + nl > z->len) return -1;
        if (nl == name_len) {
            uint32_t k = 0;
            while (k < nl && z->d[p + 46 + k] == (uint8_t)name[k]) k++;
            if (k == nl) {
                uint32_t lh = zip_u32(z->d + p + 42);
                if (lh + 30 > z->len || zip_u32(z->d + lh) != 0x04034b50u) return -1;
                e->name = z->d + p + 46; e->name_len = nl;
                e->method = zip_u16(z->d + p + 10);
                e->csize = zip_u32(z->d + p + 20);
                e->usize = zip_u32(z->d + p + 24);
                e->data_off = lh + 30 + zip_u16(z->d + lh + 26) + zip_u16(z->d + lh + 28);
                return e->data_off + e->csize <= z->len ? 0 : -1;
            }
        }
        p += 46 + nl + xl + cl;
    }
    return -1;
}

/* Unpacks an entry into out. *outlen: in = room, out = bytes written. Returns 0 when ok. */
static int zip_read(const struct zip *z, const struct zip_entry *e, uint8_t *out, uint32_t *outlen)
{
    if (e->method == 0) {
        if (e->csize > *outlen) return -1;
        for (uint32_t i = 0; i < e->csize; i++) out[i] = z->d[e->data_off + i];
        *outlen = e->csize;
        return 0;
    }
    if (e->method != 8) return -1;
    return inflate_raw(z->d + e->data_off, e->csize, out, outlen);
}
