/*
 * Inflate: unpacks "deflate" data (the compression inside ZIP and EPUB files).
 * Written for Nihil after the method described in RFC 1951; the structure
 * follows Mark Adler's public-domain "puff" reference decoder.
 * No memory allocation, no libraries.
 *
 * inflate_raw() returns 0 when the data unpacked cleanly, non-zero otherwise.
 */
#pragma once
#include <stdint.h>

struct inf {
    const uint8_t *in; uint32_t inlen, incnt;
    uint8_t *out; uint32_t outlen, outcnt;
    uint32_t bitbuf; int bitcnt;
    int err;
};
struct inf_huff { uint16_t count[16]; uint16_t symbol[288]; };

static uint32_t inf_bits(struct inf *s, int need)
{
    uint32_t v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt == s->inlen) { s->err = 1; return 0; }
        v |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = need < 32 ? v >> need : 0;
    s->bitcnt -= need;
    return need < 32 ? v & ((1u << need) - 1u) : v;
}

static int inf_decode(struct inf *s, const struct inf_huff *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= (int)inf_bits(s, 1);
        if (s->err) return -1;
        int count = h->count[len];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count; first += count;
        first <<= 1; code <<= 1;
    }
    s->err = 2;
    return -1;
}

static int inf_construct(struct inf_huff *h, const uint16_t *length, int n)
{
    uint16_t offs[16];
    for (int len = 0; len <= 15; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (uint16_t)sym;
    return left;
}

static void inf_codes(struct inf *s, const struct inf_huff *lencode, const struct inf_huff *distcode)
{
    static const uint16_t lens[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint16_t lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dists[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint16_t dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        int sym = inf_decode(s, lencode);
        if (sym < 0 || s->err) return;
        if (sym < 256) {
            if (s->outcnt == s->outlen) { s->err = 3; return; }
            s->out[s->outcnt++] = (uint8_t)sym;
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = 4; return; }
            uint32_t len = lens[sym] + inf_bits(s, lext[sym]);
            sym = inf_decode(s, distcode);
            if (sym < 0 || sym >= 30 || s->err) { s->err = s->err ? s->err : 5; return; }
            uint32_t dist = dists[sym] + inf_bits(s, dext[sym]);
            if (s->err) return;
            if (dist > s->outcnt) { s->err = 6; return; }
            if (s->outcnt + len > s->outlen) { s->err = 3; return; }
            while (len--) { s->out[s->outcnt] = s->out[s->outcnt - dist]; s->outcnt++; }
        }
    }
}

static void inf_fixed(struct inf *s)
{
    struct inf_huff lencode, distcode;
    uint16_t lengths[288];
    int sym;
    for (sym = 0; sym < 144; sym++) lengths[sym] = 8;
    for (; sym < 256; sym++) lengths[sym] = 9;
    for (; sym < 280; sym++) lengths[sym] = 7;
    for (; sym < 288; sym++) lengths[sym] = 8;
    inf_construct(&lencode, lengths, 288);
    for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
    inf_construct(&distcode, lengths, 30);
    inf_codes(s, &lencode, &distcode);
}

static void inf_dynamic(struct inf *s)
{
    static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    struct inf_huff lencode, distcode;
    uint16_t lengths[320];
    int nlen = (int)inf_bits(s, 5) + 257, ndist = (int)inf_bits(s, 5) + 1, ncode = (int)inf_bits(s, 4) + 4, index;
    if (s->err) return;
    if (nlen > 286 || ndist > 30) { s->err = 7; return; }
    for (index = 0; index < ncode; index++) lengths[order[index]] = (uint16_t)inf_bits(s, 3);
    for (; index < 19; index++) lengths[order[index]] = 0;
    if (s->err) return;
    if (inf_construct(&lencode, lengths, 19) != 0) { s->err = 8; return; }
    index = 0;
    while (index < nlen + ndist) {
        int sym = inf_decode(s, &lencode);
        if (sym < 0 || s->err) return;
        if (sym < 16) {
            lengths[index++] = (uint16_t)sym;
        } else {
            int len = 0, rep;
            if (sym == 16) {
                if (index == 0) { s->err = 9; return; }
                len = lengths[index - 1];
                rep = 3 + (int)inf_bits(s, 2);
            } else if (sym == 17) {
                rep = 3 + (int)inf_bits(s, 3);
            } else {
                rep = 11 + (int)inf_bits(s, 7);
            }
            if (s->err) return;
            if (index + rep > nlen + ndist) { s->err = 10; return; }
            while (rep--) lengths[index++] = (uint16_t)len;
        }
    }
    if (lengths[256] == 0) { s->err = 11; return; }
    int e = inf_construct(&lencode, lengths, nlen);
    if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1)) { s->err = 12; return; }
    e = inf_construct(&distcode, lengths + nlen, ndist);
    if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1)) { s->err = 13; return; }
    inf_codes(s, &lencode, &distcode);
}

/* Unpacks raw deflate data. *outlen: in = room in out, out = bytes produced. */
static int inflate_raw(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t *outlen)
{
    struct inf s = { in, inlen, 0, out, *outlen, 0, 0, 0, 0 };
    int last;
    do {
        last = (int)inf_bits(&s, 1);
        uint32_t type = inf_bits(&s, 2);
        if (s.err) break;
        if (type == 0) {
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.incnt + 4 > s.inlen) { s.err = 1; break; }
            uint32_t len = s.in[s.incnt] | (uint32_t)s.in[s.incnt + 1] << 8;
            uint32_t nlen = s.in[s.incnt + 2] | (uint32_t)s.in[s.incnt + 3] << 8;
            s.incnt += 4;
            if (len != (~nlen & 0xFFFFu)) { s.err = 14; break; }
            if (s.incnt + len > s.inlen) { s.err = 1; break; }
            if (s.outcnt + len > s.outlen) { s.err = 3; break; }
            while (len--) s.out[s.outcnt++] = s.in[s.incnt++];
        } else if (type == 1) {
            inf_fixed(&s);
        } else if (type == 2) {
            inf_dynamic(&s);
        } else {
            s.err = 15;
        }
    } while (!last && !s.err);
    *outlen = s.outcnt;
    return s.err;
}
