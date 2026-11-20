/*
 * EPUB -> Nihil text stream.
 *
 * An EPUB is a ZIP file: META-INF/container.xml names the "package" file
 * (.opf); the package lists every file (manifest) and the reading order
 * (spine); each spine entry is an XHTML page (EPUB 3.3 spec, w3.org/TR/epub-33).
 *
 * The whole book is turned into one UTF-8 text stream with one paragraph per
 * line and a few control bytes (see reader.h): RD_BREAK starts a new page
 * (each spine entry), RD_HEAD marks a heading, RD_NOINDENT a line after <br>,
 * RD_ITAL_ON / RD_ITAL_OFF italics. Pictures, tables' layout and CSS are
 * ignored for now.
 */
#pragma once
#include <stdint.h>
#include "zip.h"
#include "reader.h"

/* Called now and then during long work, so the platform can keep its watchdog happy. */
static void hal_busy(void);

struct epub_out { uint8_t *p; uint32_t max, len; uint32_t col; int head, noindent, ital, space; };

static int ep_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ep_is_space(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
static int ep_eq(const uint8_t *a, uint32_t n, const char *b)          /* a[0..n) equals b, ignoring case */
{
    uint32_t i = 0;
    for (; i < n && b[i]; i++)
        if (ep_lower(a[i]) != b[i]) return 0;
    return i == n && !b[i];
}
static uint32_t ep_find(const uint8_t *d, uint32_t from, uint32_t len, const char *s)   /* returns len if absent */
{
    uint32_t n = 0;
    while (s[n]) n++;
    for (uint32_t i = from; i + n <= len; i++) {
        uint32_t k = 0;
        while (k < n && ep_lower(d[i + k]) == s[k]) k++;
        if (k == n) return i;
    }
    return len;
}
/* Value of attribute `name` inside the tag d[from..to). Returns its length, sets *at. 0 if absent. */
static uint32_t ep_attr(const uint8_t *d, uint32_t from, uint32_t to, const char *name, uint32_t *at)
{
    uint32_t n = 0;
    while (name[n]) n++;
    for (uint32_t i = from; i + n + 2 < to; i++) {
        if (!ep_is_space(d[i])) continue;
        uint32_t k = 0;
        while (k < n && ep_lower(d[i + 1 + k]) == name[k]) k++;
        if (k != n) continue;
        uint32_t j = i + 1 + n;
        while (j < to && ep_is_space(d[j])) j++;
        if (j >= to || d[j] != '=') continue;
        j++;
        while (j < to && ep_is_space(d[j])) j++;
        if (j >= to || (d[j] != '"' && d[j] != '\'')) continue;
        uint8_t q = d[j++];
        uint32_t e = j;
        while (e < to && d[e] != q) e++;
        *at = j;
        return e - j;
    }
    return 0;
}

static void ep_put_raw(struct epub_out *o, uint8_t b) { if (o->len < o->max) o->p[o->len++] = b; }
static void ep_newline(struct epub_out *o)
{
    if (o->col) { ep_put_raw(o, '\n'); o->col = 0; }
    o->space = 0;
}
static void ep_put_cp(struct epub_out *o, uint32_t c)
{
    if (c == 0xA0) c = ' ';
    if (c < 32 || c == ' ') { if (o->col) o->space = 1; return; }
    if (o->col == 0) {                                   /* first visible character of a line: markers first */
        if (o->head) ep_put_raw(o, RD_HEAD);
        else if (o->noindent) ep_put_raw(o, RD_NOINDENT);
        if (o->ital > 0) ep_put_raw(o, RD_ITAL_ON);
        o->noindent = 0;
    } else if (o->space) {
        ep_put_raw(o, ' ');
    }
    o->space = 0;
    if (c < 0x80) ep_put_raw(o, (uint8_t)c);
    else if (c < 0x800) { ep_put_raw(o, (uint8_t)(0xC0 | c >> 6)); ep_put_raw(o, (uint8_t)(0x80 | (c & 63))); }
    else if (c < 0x10000) { ep_put_raw(o, (uint8_t)(0xE0 | c >> 12)); ep_put_raw(o, (uint8_t)(0x80 | (c >> 6 & 63))); ep_put_raw(o, (uint8_t)(0x80 | (c & 63))); }
    else { ep_put_raw(o, (uint8_t)(0xF0 | c >> 18)); ep_put_raw(o, (uint8_t)(0x80 | (c >> 12 & 63))); ep_put_raw(o, (uint8_t)(0x80 | (c >> 6 & 63))); ep_put_raw(o, (uint8_t)(0x80 | (c & 63))); }
    o->col++;
}

/* &name; and &#number; */
static uint32_t ep_entity(const uint8_t *d, uint32_t n)
{
    static const struct { const char *n; uint16_t c; } t[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", ' '},
        {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
        {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"deg", 0xB0},
        {"laquo", 0xAB}, {"raquo", 0xBB}, {"shy", 0}, {"ensp", ' '}, {"emsp", ' '}, {"thinsp", ' '},
        {"eacute", 0xE9}, {"egrave", 0xE8}, {"ecirc", 0xEA}, {"euml", 0xEB}, {"aacute", 0xE1}, {"agrave", 0xE0},
        {"acirc", 0xE2}, {"atilde", 0xE3}, {"auml", 0xE4}, {"ccedil", 0xE7}, {"iacute", 0xED}, {"icirc", 0xEE},
        {"iuml", 0xEF}, {"oacute", 0xF3}, {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ouml", 0xF6}, {"uacute", 0xFA},
        {"ucirc", 0xFB}, {"uuml", 0xFC}, {"ntilde", 0xF1}, {"szlig", 0xDF}, {"aelig", 0xE6}, {"oelig", 0x153},
        {"pound", 0xA3}, {"euro", 0x20AC}, {"sect", 0xA7}, {"para", 0xB6}, {"middot", 0xB7}, {"bull", 0x2022},
        {"frac12", 0xBD}, {"frac14", 0xBC}, {"times", 0xD7},
    };
    if (n > 1 && d[0] == '#') {
        uint32_t v = 0, i = 1, hex = d[1] == 'x' || d[1] == 'X';
        for (i += hex; i < n; i++) {
            uint32_t c = (uint32_t)ep_lower(d[i]);
            if (c >= '0' && c <= '9') v = v * (hex ? 16 : 10) + (c - '0');
            else if (hex && c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
            else return '?';
        }
        return v;
    }
    for (uint32_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        uint32_t k = 0;
        while (k < n && t[i].n[k] && d[k] == (uint8_t)t[i].n[k]) k++;      /* case matters: &Eacute; is not listed */
        if (k == n && !t[i].n[k]) return t[i].c;
    }
    return '?';
}

/* One XHTML page -> text appended to o. */
static void ep_html(const uint8_t *d, uint32_t len, struct epub_out *o)
{
    static const char *const block[] = { "p", "div", "li", "tr", "blockquote", "section", "article", "ul", "ol", "table",
                                         "hr", "dt", "dd", "figure", "figcaption", "pre", "header", "footer", "aside", "nav", 0 };
    static const char *const skip[] = { "script", "style", "head", "svg", "math", 0 };
    uint32_t i = ep_find(d, 0, len, "<body");
    if (i == len) i = 0;
    o->head = 0; o->ital = 0; o->noindent = 0;
    while (i < len) {
        uint8_t c = d[i];
        if (c == '<') {
            if (i + 3 < len && d[i + 1] == '!' && d[i + 2] == '-' && d[i + 3] == '-') {
                uint32_t e = ep_find(d, i + 4, len, "-->");
                i = e == len ? len : e + 3;
                continue;
            }
            uint32_t e = i + 1;
            while (e < len && d[e] != '>') e++;
            uint32_t s = i + 1;
            int closing = s < e && d[s] == '/';
            s += (uint32_t)closing;
            uint32_t n = s;
            while (n < e && !ep_is_space(d[n]) && d[n] != '/') n++;
            const uint8_t *name = d + s;
            uint32_t nl = n - s;
            int done = 0;
            for (uint32_t k = 0; skip[k] && !done; k++)
                if (!closing && ep_eq(name, nl, skip[k]) && !(e > i && d[e - 1] == '/')) {
                    char endtag[16] = "</";
                    uint32_t q = 2;
                    for (const char *t = skip[k]; *t; t++) endtag[q++] = *t;
                    endtag[q] = 0;
                    uint32_t f = ep_find(d, e, len, endtag);
                    e = f;
                    while (e < len && d[e] != '>') e++;
                    done = 1;
                }
            if (!done) {
                if (nl == 2 && ep_lower(name[0]) == 'h' && name[1] >= '1' && name[1] <= '6') {
                    ep_newline(o);
                    o->head = !closing;
                } else if (ep_eq(name, nl, "br")) {
                    if (o->col) { ep_newline(o); o->noindent = 1; }
                } else if (ep_eq(name, nl, "i") || ep_eq(name, nl, "em") || ep_eq(name, nl, "cite")) {
                    if (!(e > i && d[e - 1] == '/')) {
                        if (!closing) { if (o->ital++ == 0 && o->col) ep_put_raw(o, RD_ITAL_ON); }
                        else if (o->ital > 0) { if (--o->ital == 0 && o->col) ep_put_raw(o, RD_ITAL_OFF); }
                    }
                } else {
                    for (uint32_t k = 0; block[k]; k++)
                        if (ep_eq(name, nl, block[k])) { ep_newline(o); o->noindent = 0; break; }
                }
            }
            i = e < len ? e + 1 : len;
        } else if (c == '&') {
            uint32_t e = i + 1;
            while (e < len && e < i + 12 && d[e] != ';' && d[e] != '<' && !ep_is_space(d[e])) e++;
            if (e < len && d[e] == ';') { uint32_t v = ep_entity(d + i + 1, e - i - 1); if (v) ep_put_cp(o, v); i = e + 1; }
            else { ep_put_cp(o, '&'); i++; }
        } else if (c < 0x80) {
            ep_put_cp(o, c);
            i++;
        } else {                                          /* UTF-8 sequence: decode, re-encode */
            uint32_t v = c, n = 0;
            if (c >= 0xF0) { v &= 7; n = 3; } else if (c >= 0xE0) { v &= 15; n = 2; } else if (c >= 0xC0) { v &= 31; n = 1; } else v = '?';
            i++;
            while (n-- && i < len && (d[i] & 0xC0) == 0x80) v = v << 6 | (d[i++] & 0x3F);
            if (v != 0xAD && v != 0xFEFF) ep_put_cp(o, v);   /* soft hyphen, byte-order mark: invisible */
        }
    }
    ep_newline(o);
}

/* Joins the package's folder and a link ("../Text/a%20b.xhtml#x") into a ZIP entry name. */
static uint32_t ep_path(const char *base, uint32_t base_len, const uint8_t *href, uint32_t href_len, char *out, uint32_t max)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < base_len && n + 1 < max; i++) out[n++] = base[i];
    for (uint32_t i = 0; i < href_len && n + 1 < max; i++) {
        uint8_t c = href[i];
        if (c == '#') break;
        if (c == '%' && i + 2 < href_len) {
            uint32_t h = (uint32_t)ep_lower(href[i + 1]), l = (uint32_t)ep_lower(href[i + 2]);
            h = h >= 'a' ? h - 'a' + 10 : h - '0'; l = l >= 'a' ? l - 'a' + 10 : l - '0';
            c = (uint8_t)(h << 4 | l); i += 2;
        }
        if (c == '.' && i + 2 < href_len && href[i + 1] == '.' && href[i + 2] == '/' && (n == 0 || out[n - 1] == '/')) {
            if (n) n--;                                   /* "../": drop the last folder */
            while (n && out[n - 1] != '/') n--;
            i += 2;
            continue;
        }
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}

/*
 * Converts the EPUB in file[0..len) into text. `work` is scratch memory
 * (work_max bytes; needs room for the package file plus the largest page).
 * Returns 0 when ok, else a small error number (1 not a ZIP, 2 no container,
 * 3 no package, 4 nothing readable).
 * *text_len = bytes written to text; title gets the book title (may be empty).
 */
static int epub_to_text(const uint8_t *file, uint32_t len, uint8_t *work, uint32_t work_max,
                        uint8_t *text, uint32_t text_max, uint32_t *text_len, char *title, uint32_t title_max)
{
    struct zip z;
    struct zip_entry e;
    char path[512];
    uint32_t n, at;
    title[0] = 0; *text_len = 0;
    if (zip_open(&z, file, len)) return 1;
    if (zip_find(&z, "META-INF/container.xml", 22, &e)) return 2;
    n = work_max;
    if (zip_read(&z, &e, work, &n)) return 2;
    uint32_t rf = ep_find(work, 0, n, "<rootfile ");
    if (rf == n) rf = ep_find(work, 0, n, "<rootfile\n");
    uint32_t rfe = rf;
    while (rfe < n && work[rfe] != '>') rfe++;
    uint32_t pl = ep_attr(work, rf, rfe, "full-path", &at);
    if (!pl || pl >= sizeof path) return 3;
    uint32_t dir_len = 0;
    for (uint32_t i = 0; i < pl; i++) { path[i] = (char)work[at + i]; if (path[i] == '/') dir_len = i + 1; }
    char dir[256];
    if (dir_len >= sizeof dir) return 3;
    for (uint32_t i = 0; i < dir_len; i++) dir[i] = path[i];
    if (zip_find(&z, path, pl, &e)) return 3;
    uint32_t opf_len = work_max;
    if (zip_read(&z, &e, work, &opf_len)) return 3;
    const uint8_t *opf = work;
    uint8_t *page = work + ((opf_len + 15u) & ~15u);
    uint32_t page_max = work_max - ((opf_len + 15u) & ~15u);

    /* title */
    uint32_t t = ep_find(opf, 0, opf_len, "<dc:title");
    if (t < opf_len) {
        while (t < opf_len && opf[t] != '>') t++;
        uint32_t k = 0;
        for (t++; t < opf_len && opf[t] != '<' && k + 1 < title_max; t++) title[k++] = (char)opf[t];
        title[k] = 0;
    }

    struct epub_out o = { text, text_max, 0, 0, 0, 0, 0, 0 };
    uint32_t spine = ep_find(opf, 0, opf_len, "<spine"), pages = 0;
    for (uint32_t s = ep_find(opf, spine, opf_len, "<itemref"); s < opf_len; s = ep_find(opf, s + 8, opf_len, "<itemref")) {
        uint32_t se = s;
        while (se < opf_len && opf[se] != '>') se++;
        uint32_t idl = ep_attr(opf, s, se, "idref", &at), id_at = at;
        if (!idl) continue;
        /* find the manifest item with this id */
        for (uint32_t m = ep_find(opf, 0, opf_len, "<item"); m < opf_len; m = ep_find(opf, m + 5, opf_len, "<item")) {
            if (!ep_is_space(opf[m + 5])) continue;      /* <itemref> is not <item> */
            uint32_t me = m;
            while (me < opf_len && opf[me] != '>') me++;
            uint32_t il = ep_attr(opf, m, me, "id", &at);
            if (il != idl) continue;
            uint32_t k = 0;
            while (k < il && opf[at + k] == opf[id_at + k]) k++;
            if (k != il) continue;
            uint32_t hl = ep_attr(opf, m, me, "href", &at);
            if (!hl) break;
            uint32_t pn = ep_path(dir, dir_len, opf + at, hl, path, sizeof path);
            if (zip_find(&z, path, pn, &e)) break;
            n = page_max;
            if (zip_read(&z, &e, page, &n)) break;
            hal_busy();
            ep_newline(&o);
            if (o.len) ep_put_raw(&o, RD_BREAK);
            uint32_t before = o.len;
            ep_html(page, n, &o);
            if (o.len == before && o.len) o.len--;       /* empty page (cover picture): no page break for it */
            else pages++;
            break;
        }
    }
    *text_len = o.len;
    return pages ? 0 : 4;
}
