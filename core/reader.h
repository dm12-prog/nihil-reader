/*
 * Nihil reader core: turns plain text (UTF-8) into pages of typeset lines.
 * No hardware access in here: the same code runs on the Sage and on a PC
 * (tools/preview) so a page can be looked at before it goes on a device.
 *
 * The picture is always stored in the panel's own shape (pw x ph). "Page"
 * coordinates are what the reader sees; rd.rot says how the page sits on the
 * panel: 0 = same way, 1 = page top at the panel's right edge, 2 = upside
 * down, 3 = page top at the panel's left edge.
 */
#pragma once
#include <stdint.h>

struct glyph { uint16_t cp; uint8_t w, h; int8_t xoff, yoff; uint8_t adv; uint32_t off; };
struct font { uint32_t size, ascent, descent, nglyphs; const struct glyph *glyphs; const uint8_t *ink; };
#include "font_data.h"

/* Control bytes inside the text (they take no room on the page). */
enum { RD_HEAD = 1,      /* at the start of a paragraph: it is a heading */
       RD_NOINDENT = 2,  /* at the start of a paragraph: no first-line indent (line after <br>) */
       RD_ITAL_ON = 3, RD_ITAL_OFF = 4,   /* italics; switched off by every paragraph end */
       RD_BREAK = 12 };  /* at the start of a paragraph: begin a new page (chapter) */

struct reader {
    volatile uint8_t *img;      /* picture being drawn, 1 byte per pixel, 255 = white */
    uint32_t pw, ph, rot;
    const uint8_t *text;        /* the book */
    uint32_t len;
    uint32_t margin_x, margin_top, margin_bottom, line_h, indent;
    uint32_t footer_h;          /* room kept under the text for the footer line */
    int justify;                /* 1 = stretch lines to both margins, 0 = ragged right */
    const struct font *body, *ital, *head, *small;
};

static uint32_t rd_page_w(const struct reader *r) { return (r->rot & 1) ? r->ph : r->pw; }
static uint32_t rd_page_h(const struct reader *r) { return (r->rot & 1) ? r->pw : r->ph; }

static void rd_ink(const struct reader *r, uint32_t x, uint32_t y, uint32_t ink)   /* ink 0..15 */
{
    uint32_t X, Y;
    if (x >= rd_page_w(r) || y >= rd_page_h(r))
        return;
    switch (r->rot) {
    case 1:  X = r->pw - 1 - y; Y = x;             break;
    case 2:  X = r->pw - 1 - x; Y = r->ph - 1 - y; break;
    case 3:  X = y;             Y = r->ph - 1 - x; break;
    default: X = x;             Y = y;             break;
    }
    uint8_t v = (uint8_t)(255u - ink * 17u);
    volatile uint8_t *p = &r->img[Y * r->pw + X];
    if (v < *p)
        *p = v;
}

static const struct glyph *rd_glyph(const struct font *f, uint32_t cp)
{
    if (cp >= 32 && cp < 127)
        return &f->glyphs[cp - 32];
    if (cp >= 160 && cp < 256)
        return &f->glyphs[95 + cp - 160];
    for (uint32_t i = 191; i < f->nglyphs; i++)
        if (f->glyphs[i].cp == cp)
            return &f->glyphs[i];
    return &f->glyphs['?' - 32];
}

/* Reads one character (UTF-8) from t at *pos and moves *pos past it. */
static uint32_t rd_utf8(const uint8_t *t, uint32_t len, uint32_t *pos)
{
    uint32_t i = *pos, c = t[i++], n = 0;
    if (c >= 0xF0) { c &= 7; n = 3; }
    else if (c >= 0xE0) { c &= 15; n = 2; }
    else if (c >= 0xC0) { c &= 31; n = 1; }
    else if (c >= 0x80) { c = '?'; }
    while (n-- && i < len && (t[i] & 0xC0) == 0x80)
        c = (c << 6) | (t[i++] & 0x3F);
    *pos = i;
    if (c == 0xA0 || c == '\t' || c == '\r')
        c = ' ';
    return c;
}
static uint32_t rd_next(const struct reader *r, uint32_t *pos) { return rd_utf8(r->text, r->len, pos); }

static uint32_t rd_draw_cp(const struct reader *r, const struct font *f, uint32_t x, uint32_t y, uint32_t cp)
{
    const struct glyph *g = rd_glyph(f, cp);
    const uint8_t *ink = &f->ink[g->off];
    for (uint32_t gy = 0; gy < g->h; gy++)
        for (uint32_t gx = 0; gx < g->w; gx++) {
            uint32_t v = ink[gy * g->w + gx];
            if (v)
                rd_ink(r, (uint32_t)((int32_t)x + g->xoff) + gx, (uint32_t)((int32_t)y + g->yoff) + gy, v);
        }
    return g->adv;
}

static uint32_t rd_draw_ascii(const struct reader *r, const struct font *f, uint32_t x, uint32_t y, const char *s)
{
    for (; *s; s++)
        x += rd_draw_cp(r, f, x, y, (uint8_t)*s);
    return x;
}
static uint32_t rd_width_ascii(const struct font *f, const char *s)
{
    uint32_t w = 0;
    for (; *s; s++)
        w += rd_glyph(f, (uint8_t)*s)->adv;
    return w;
}

/* A filled rectangle in page coordinates, ink 0..15. */
static void rd_rect(const struct reader *r, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t ink)
{
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            rd_ink(r, x + i, y + j, ink);
}

static uint32_t rd_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* Width of a UTF-8 string. */
static uint32_t rd_width(const struct font *f, const char *s)
{
    uint32_t w = 0, p = 0, n = rd_strlen(s);
    while (p < n)
        w += rd_glyph(f, rd_utf8((const uint8_t *)s, n, &p))->adv;
    return w;
}

/* Draws a UTF-8 string on one line; if it is wider than max_w it is cut and
 * ends in "...". Returns the x after the text. */
static uint32_t rd_draw(const struct reader *r, const struct font *f, uint32_t x, uint32_t y, const char *s, uint32_t max_w)
{
    uint32_t p = 0, n = rd_strlen(s), x0 = x, dots = rd_glyph(f, 0x2026)->adv;
    int cut = rd_width(f, s) > max_w;
    while (p < n) {
        uint32_t c = rd_utf8((const uint8_t *)s, n, &p);
        if (c < 32) continue;
        if (cut && x - x0 + rd_glyph(f, c)->adv + dots > max_w) {
            x += rd_draw_cp(r, f, x, y, 0x2026);
            break;
        }
        x += rd_draw_cp(r, f, x, y, c);
    }
    return x;
}

/* What style is in force at text offset pos (start of a line)? Looks back to
 * the start of the paragraph: styles never carry across paragraphs. */
static void rd_style_at(const struct reader *r, uint32_t pos, int *heading, int *noindent, int *ital)
{
    uint32_t p = pos;
    while (p > 0 && r->text[p - 1] != '\n')
        p--;
    *heading = 0; *noindent = 0; *ital = 0;
    for (; p < pos; p++) {
        uint8_t c = r->text[p];
        if (c == RD_HEAD) *heading = 1;
        else if (c == RD_ITAL_ON) *ital = 1;
        else if (c == RD_ITAL_OFF) *ital = 0;
    }
    for (; p < r->len && r->text[p] < 32 && r->text[p] != '\n'; p++) {   /* markers sitting right at pos */
        if (r->text[p] == RD_HEAD) *heading = 1;
        if (r->text[p] == RD_NOINDENT) *noindent = 1;
    }
}

static uint32_t rd_line_height(const struct reader *r, int heading)
{
    return heading ? r->head->size * 3u / 2u : r->line_h;
}

/* Lays out one line starting at text offset `pos`.
 * Returns the offset where the next line starts. If `draw` is set the line is
 * painted at height y. A paragraph ends at a line feed. */
static uint32_t rd_line(const struct reader *r, uint32_t pos, uint32_t y, int draw)
{
    uint32_t width = rd_page_w(r) - 2 * r->margin_x;
    uint32_t x0 = r->margin_x;
    int para_start = pos == 0 || r->text[pos - 1] == '\n' || r->text[pos - 1] == RD_BREAK;
    int heading, noindent, ital0;
    rd_style_at(r, pos, &heading, &noindent, &ital0);
    if (para_start && !heading && !noindent) { x0 += r->indent; width -= r->indent; }

    /* pass 1: how much fits */
    uint32_t p = pos, used = 0, gaps = 0;
    uint32_t brk = pos, brk_used = 0, brk_gaps = 0;      /* last place a line may end */
    int ended = 0, in_space = 1, ital = ital0;           /* leading spaces are skipped */
    while (p < r->len) {
        uint32_t q = p, c = rd_next(r, &q);
        if (c == '\n') { p = q; ended = 1; break; }
        if (c < 32) {
            if (c == RD_ITAL_ON) ital = 1;
            if (c == RD_ITAL_OFF) ital = 0;
            p = q;
            continue;
        }
        const struct font *f = heading ? r->head : ital ? r->ital : r->body;
        if (c == ' ') {
            if (!in_space) { brk = p; brk_used = used; brk_gaps = gaps; }
            in_space = 1;
            p = q;
            continue;
        }
        uint32_t add = rd_glyph(f, c)->adv, sp = 0;
        if (in_space && used > 0) { sp = rd_glyph(f, ' ')->adv; }
        if (used + sp + add > width) {
            if (brk == pos && used > 0) { brk = p; brk_used = used; brk_gaps = gaps; }   /* one very long word: cut it */
            break;
        }
        if (sp) gaps++;
        used += sp + add;
        in_space = 0;
        p = q;
    }
    uint32_t end, next;
    int justify = 0;
    if (ended || p >= r->len) {
        end = p; next = p;                                /* last line of a paragraph: not stretched */
    } else if (brk != pos) {
        end = brk; next = brk; used = brk_used; gaps = brk_gaps; justify = r->justify && !heading;
    } else {
        end = p; next = p;                                /* nothing fitted (cannot happen with sane margins) */
        if (end == pos) { uint32_t q = pos; rd_next(r, &q); next = q; }
    }
    if (!draw)
        return next;

    /* pass 2: paint */
    uint32_t extra = (justify && gaps && width > used) ? width - used : 0;
    if (extra > width / 4)
        extra = 0;                                        /* too loose: leave it ragged */
    uint32_t x = heading && width > used ? x0 + (width - used) / 2 : x0, gap_i = 0, acc = 0;
    if (heading)
        y += (rd_line_height(r, 1) - r->head->size) / 2;  /* sit in the middle of its taller line */
    in_space = 1; ital = ital0;
    int first = 1;
    for (p = pos; p < end;) {
        uint32_t c = rd_next(r, &p);
        if (c == '\n')
            continue;
        if (c < 32) {
            if (c == RD_ITAL_ON) ital = 1;
            if (c == RD_ITAL_OFF) ital = 0;
            continue;
        }
        const struct font *f = heading ? r->head : ital ? r->ital : r->body;
        if (c == ' ') { in_space = 1; continue; }
        if (in_space && !first) {
            x += rd_glyph(f, ' ')->adv;
            if (extra) {                                  /* share the spare width between the gaps */
                gap_i++;
                uint32_t want = extra * gap_i / gaps;
                x += want - acc;
                acc = want;
            }
        }
        in_space = 0; first = 0;
        x += rd_draw_cp(r, f, x, y, c);
    }
    return next;
}

/* Paints the page that starts at text offset `pos` (or only measures it when
 * draw = 0). Returns the offset where the following page starts. */
static uint32_t rd_page(const struct reader *r, uint32_t pos, int draw)
{
    uint32_t top = r->margin_top, y = top, bottom = rd_page_h(r) - r->margin_bottom - r->footer_h;
    while (pos < r->len) {
        uint8_t c = r->text[pos];
        if (c == '\n' || c == '\r') { pos++; continue; }  /* empty lines */
        if (c == RD_BREAK) {
            if (y > top) break;                           /* new chapter: this page ends here */
            pos++;
            continue;
        }
        int heading, noindent, ital;
        rd_style_at(r, pos, &heading, &noindent, &ital);
        int para_start = pos == 0 || r->text[pos - 1] == '\n' || r->text[pos - 1] == RD_BREAK;
        uint32_t h = rd_line_height(r, heading);
        if (heading && para_start && y > top)
            y += r->line_h / 2;                           /* air above a heading */
        if (y + h > bottom) break;
        pos = rd_line(r, pos, y, draw);
        y += h;
        if (heading && pos < r->len && r->text[pos] == '\n')
            y += r->line_h / 4;                           /* and a little below it */
    }
    while (pos < r->len && (r->text[pos] == '\n' || r->text[pos] == '\r'))
        pos++;
    return pos;
}

static char *rd_utoa(uint32_t n, char *end)                 /* writes backwards from end, returns start */
{
    *--end = 0;
    do { *--end = (char)('0' + n % 10); n /= 10; } while (n);
    return end;
}

/* Footer: page number on the left, how far through the book on the right. */
static void rd_footer(const struct reader *r, uint32_t page_no, uint32_t pos)
{
    char b[16], s[32];
    uint32_t y = rd_page_h(r) - r->margin_bottom - r->small->ascent - r->small->descent, n = 0;
    rd_draw_ascii(r, r->small, r->margin_x, y, rd_utoa(page_no, b + sizeof b));
    uint32_t pct = pos >= r->len ? 100u : (pos / 64u * 100u) / (r->len / 64u + 1u);   /* 32-bit maths only */
    for (const char *t = rd_utoa(pct, b + sizeof b); *t; t++) s[n++] = *t;
    s[n++] = '%'; s[n] = 0;
    rd_draw_ascii(r, r->small, rd_page_w(r) - r->margin_x - rd_width_ascii(r->small, s), y, s);
}

/* Page look copied from Diogo's Kobalt settings (reader-settings.json on his
 * Kobo, 20 Nov 2026): Libron 46 px, line spacing 150 %, left aligned, no
 * hyphenation, side margins 51 px, top and bottom margins 27 px. */
static void rd_defaults(struct reader *r)
{
    r->body = &font_body; r->ital = &font_ital; r->head = &font_head; r->small = &font_small;
    r->margin_x = 51; r->margin_top = 27; r->margin_bottom = 27;
    r->line_h = 69; r->indent = 46; r->justify = 0;
    r->footer_h = 60;
}
