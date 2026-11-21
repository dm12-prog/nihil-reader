/*
 * Nihil app: what the reader does with books and two buttons.
 * Hardware-free. The Sage build (platform/sage/main.c) and the PC simulator
 * (tools/sim) both drive this same code, so what the simulator shows is what
 * the device will draw.
 *
 * Buttons (the Sage has two page buttons and nothing else we can use yet):
 *   reading:  short press = turn the page; hold the upper button = menu
 *   lists:    short press = move down / up; hold the lower button = choose;
 *             hold the upper button = go back
 *
 * Cover: closing it puts Nihil to sleep (buttons ignored, light off), opening
 * it wakes it up where it was. The page stays on the screen: e-ink needs no
 * power to keep showing it.
 *
 * The platform supplies three services (see the hal_ functions below).
 */
#pragma once
#include "reader.h"
#include "epub.h"

enum app_key { KEY_NEXT, KEY_BACK, KEY_NEXT_HOLD, KEY_BACK_HOLD };
enum app_view { VIEW_BOOK, VIEW_MENU, VIEW_FILES };
enum { APP_SAME = 0, APP_REDRAW = 1, APP_LEAVE = 2 };

#define APP_NAME_MAX 384           /* long file names: up to 255 characters, more bytes with accents */
#define APP_PATH_MAX 1024
#define APP_ENTRIES  512

struct app_entry { char name[APP_NAME_MAX]; uint8_t is_dir; };

/* ---- services the platform must provide ---- */
/* Lists a folder ("" = top). Fills out[0..max), returns how many (folders and books only), or -1. */
static int hal_list_dir(const char *path, struct app_entry *out, int max);
/* Reads a whole file. Returns 0 when ok and sets *len. */
static int hal_read_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *len);
/* Front light: brightness and warmth, each 0..100. */
static void hal_set_light(uint32_t brightness, uint32_t warmth);
/* hal_busy(): see epub.h. */

/* ---- reading positions ----
 * The app never stores anything itself. After every page turn it tells the
 * platform "this book is now here" (hal_pos_save), and at start it reads the
 * list of places from a small text file (POS_FILE) like any other file.
 * On the Sage the platform keeps the latest places in RAM, and Kobo's own
 * Linux copies them into the file the next time it runs (tools/kobo/positions.sh).
 * So Nihil's storage driver stays read-only.
 *
 * POS_FILE, one record per line, numbers in hex:
 *   P <path hash> <text offset> <page number> <stamp> <check>
 *   L <length> <path hash> <path bytes, 4 per word, low byte first>   the book read last
 *   S <light> <warmth>
 */
#define POS_FILE     ".adds/nihil/positions.txt"
#define POS_MAX      256
#define POS_PATH_MAX 240           /* longest path that can be remembered as "the book read last" */
#define POS_MAGIC    0x504F5331u   /* "POS1" */
#define POS_TEXT_VERSION 1u        /* raise when the way a book becomes text changes (offsets move) */
struct pos_rec { uint32_t hash, off, page, stamp; };
/* Remember: the book at `path` is at this place (r = 0: no book open); light settings as given. */
static void hal_pos_save(const struct pos_rec *r, const char *path, uint32_t light, uint32_t warmth);

struct app_mem {                   /* working memory handed over by the platform */
    uint8_t *file; uint32_t file_max;       /* a book file as it is on disk */
    uint8_t *work; uint32_t work_max;       /* scratch for unpacking */
    uint8_t *text; uint32_t text_max;       /* the book as text */
    volatile uint32_t *pages; uint32_t pages_max;
};

struct app {
    struct reader rd;
    struct app_mem m;
    enum app_view view;
    /* book */
    int have_book;
    char title[APP_NAME_MAX];
    uint32_t page, next_pos;
    /* menu */
    uint32_t menu_sel;
    uint32_t light, warmth;
    /* file list */
    char path[APP_PATH_MAX];
    struct app_entry entries[APP_ENTRIES];
    uint32_t count, sel, top;
    int asleep;                             /* cover closed */
    char note[APP_NAME_MAX + 32];           /* one-line message, e.g. why a book did not open */
    /* reading positions */
    struct pos_rec pos[POS_MAX];
    uint32_t pos_n;
    char book_path[APP_PATH_MAX];
    uint32_t book_hash, book_stamp;
    uint32_t first_known;                   /* page starts below this page number are not worked out yet */
    char last_path[POS_PATH_MAX + 1];       /* the book read last, "" = none */
    /* running header: the chapter heading in force at text offset chap_at */
    char chapter[160];
    uint32_t chap_at, chap_off;             /* chap_off = where that heading starts, 0xFFFFFFFF = none yet */
};

static void app_copy(char *d, const char *s, uint32_t max)
{
    uint32_t i = 0;
    for (; s[i] && i + 1 < max; i++) d[i] = s[i];
    d[i] = 0;
}
static void app_cat(char *d, const char *s, uint32_t max)
{
    uint32_t n = rd_strlen(d);
    app_copy(d + n, s, max - n);
}

/* ---------- reading positions ---------- */
static uint32_t pos_hash(const char *s)                   /* FNV-1a, never 0 */
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h ? h : 1u;
}
static uint32_t pos_check(const struct pos_rec *r)
{
    uint32_t c = POS_MAGIC ^ r->hash;
    c = (c << 5 | c >> 27) ^ r->off;
    c = (c << 5 | c >> 27) ^ r->page;
    c = (c << 5 | c >> 27) ^ r->stamp;
    return c * 2654435761u;
}
/* A number that changes whenever pages would break differently: page shape, margins, fonts, book length. */
static uint32_t pos_stamp(const struct reader *r, uint32_t text_len)
{
    const uint32_t v[] = { rd_page_w(r), rd_page_h(r), r->margin_x, r->margin_top, r->margin_bottom, r->line_h,
                           r->indent, r->footer_h, (uint32_t)r->justify, r->body->size, r->ital->size,
                           r->head->size, r->body->nglyphs, POS_TEXT_VERSION, text_len };
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < sizeof v / sizeof v[0]; i++) { h ^= v[i]; h *= 16777619u; }
    if (r->header_h | r->sink | r->head_gap | r->flush_first) {   /* a look other than the plain one */
        const uint32_t x[] = { r->header_h, r->sink, r->head_gap, r->flush_first };
        for (uint32_t i = 0; i < 4; i++) { h ^= x[i]; h *= 16777619u; }
    }
    return h;
}
static struct pos_rec *pos_find(struct app *a, uint32_t hash)
{
    for (uint32_t i = 0; i < a->pos_n; i++)
        if (a->pos[i].hash == hash) return &a->pos[i];
    return 0;
}
static int pos_hex(const uint8_t *d, uint32_t len, uint32_t *i, uint32_t *out)   /* next hex number on this line */
{
    uint32_t v = 0, n = 0;
    while (*i < len && d[*i] == ' ') (*i)++;
    for (; *i < len; (*i)++, n++) {
        uint32_t c = d[*i], x;
        if (c >= '0' && c <= '9') x = c - '0';
        else if (c >= 'a' && c <= 'f') x = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') x = c - 'A' + 10;
        else break;
        v = v << 4 | x;
    }
    *out = v;
    return n > 0 && n <= 8;
}
/* Reads POS_FILE. A missing or damaged file just means "no places known". */
static void pos_load(struct app *a)
{
    uint32_t len = 0, i = 0;
    a->pos_n = 0; a->last_path[0] = 0;
    uint32_t max = a->m.work_max < 65536u ? a->m.work_max : 65536u;
    if (hal_read_file(POS_FILE, a->m.work, max, &len) || len == 0) return;
    const uint8_t *d = a->m.work;
    while (i < len) {
        uint8_t kind = d[i++];
        if (kind == 'P') {
            struct pos_rec r; uint32_t chk;
            if (pos_hex(d, len, &i, &r.hash) && pos_hex(d, len, &i, &r.off) && pos_hex(d, len, &i, &r.page) &&
                pos_hex(d, len, &i, &r.stamp) && pos_hex(d, len, &i, &chk) && chk == pos_check(&r) &&
                a->pos_n < POS_MAX && !pos_find(a, r.hash))
                a->pos[a->pos_n++] = r;
        } else if (kind == 'L') {
            uint32_t n, h, w;
            char path[POS_PATH_MAX + 4];
            if (pos_hex(d, len, &i, &n) && pos_hex(d, len, &i, &h) && n > 0 && n <= POS_PATH_MAX) {
                uint32_t k = 0;
                while (k < n && pos_hex(d, len, &i, &w)) {
                    for (uint32_t b = 0; b < 4; b++) path[k++] = (char)(w >> (8 * b));
                }
                if (k >= n) {
                    path[n] = 0;
                    if (rd_strlen(path) == n && pos_hash(path) == h) app_copy(a->last_path, path, sizeof a->last_path);
                }
            }
        } else if (kind == 'S') {
            uint32_t l, w;
            if (pos_hex(d, len, &i, &l) && pos_hex(d, len, &i, &w) && l <= 100 && w <= 100) { a->light = l; a->warmth = w; }
        }
        while (i < len && d[i] != '\n') i++;              /* rest of the line */
        i++;
    }
}
/* Tell the platform where the open book is now. */
static void app_pos_note(struct app *a)
{
    if (!a->have_book) { hal_pos_save(0, "", a->light, a->warmth); return; }
    struct pos_rec r = { a->book_hash, a->m.pages[a->page], a->page, a->book_stamp };
    struct pos_rec *p = pos_find(a, r.hash);
    if (!p) p = &a->pos[a->pos_n < POS_MAX ? a->pos_n++ : POS_MAX - 1];
    *p = r;
    hal_pos_save(&r, a->book_path, a->light, a->warmth);
}
/* Works out every page start from the beginning of the book up to the page that holds `off`. */
static void app_pages_upto(struct app *a, uint32_t off)
{
    uint32_t i = 0;
    a->m.pages[0] = 0;
    while (i + 1 < a->m.pages_max) {
        uint32_t next = rd_page(&a->rd, a->m.pages[i], 0);
        if (next > off || next >= a->rd.len) break;
        a->m.pages[++i] = next;
        if (!(i & 15u)) hal_busy();
    }
    a->page = i; a->first_known = 0;
}

static void app_load_dir(struct app *a)
{
    int n = hal_list_dir(a->path, a->entries, APP_ENTRIES);
    a->count = n > 0 ? (uint32_t)n : 0;
    /* folders first, then names in order (simple insertion sort, ASCII case-blind) */
    for (uint32_t i = 1; i < a->count; i++) {
        struct app_entry e = a->entries[i];
        uint32_t j = i;
        while (j > 0) {
            const struct app_entry *o = &a->entries[j - 1];
            int after = 0;
            if (o->is_dir != e.is_dir) after = e.is_dir;
            else {
                uint32_t k = 0;
                while (o->name[k] && ep_lower((uint8_t)o->name[k]) == ep_lower((uint8_t)e.name[k])) k++;
                after = ep_lower((uint8_t)e.name[k]) < ep_lower((uint8_t)o->name[k]);
            }
            if (!after) break;
            a->entries[j] = a->entries[j - 1];
            j--;
        }
        a->entries[j] = e;
    }
    a->sel = 0; a->top = 0;
}

/* Opens the book at `path`. Returns 0 when ok; otherwise leaves a note. */
static int app_open(struct app *a, const char *path, const char *name)
{
    uint32_t flen = 0, tlen = 0;
    a->note[0] = 0;
    if (hal_read_file(path, a->m.file, a->m.file_max, &flen) || flen == 0) {
        app_copy(a->note, "Could not read ", sizeof a->note); app_cat(a->note, name, sizeof a->note);
        return -1;
    }
    char title[APP_NAME_MAX];
    title[0] = 0;
    if (flen > 4 && a->m.file[0] == 'P' && a->m.file[1] == 'K') {
        int e = epub_to_text(a->m.file, flen, a->m.work, a->m.work_max, a->m.text, a->m.text_max, &tlen, title, sizeof title);
        if (e) {
            app_copy(a->note, "Not a readable EPUB: ", sizeof a->note); app_cat(a->note, name, sizeof a->note);
            return -1;
        }
    } else {
        tlen = flen < a->m.text_max ? flen : a->m.text_max;
        for (uint32_t i = 0; i < tlen; i++) a->m.text[i] = a->m.file[i];
    }
    app_copy(a->title, title[0] ? title : name, sizeof a->title);
    a->rd.text = a->m.text; a->rd.len = tlen;
    a->page = 0; a->m.pages[0] = 0; a->next_pos = 0; a->first_known = 0;
    a->have_book = 1;
    a->view = VIEW_BOOK;
    a->chapter[0] = 0; a->chap_at = 0; a->chap_off = 0xFFFFFFFFu;
    /* back to where this book was left */
    app_copy(a->book_path, path, sizeof a->book_path);
    a->book_hash = pos_hash(path);
    a->book_stamp = pos_stamp(&a->rd, tlen);
    const struct pos_rec *p = pos_find(a, a->book_hash);
    if (p && p->off > 0 && p->off < tlen) {
        if (p->stamp == a->book_stamp && p->page > 0 && p->page < a->m.pages_max) {
            a->page = p->page;                             /* same page look as last time: jump straight there */
            a->m.pages[a->page] = p->off;
            a->first_known = a->page;
        } else {
            app_pages_upto(a, p->off);                     /* page look changed: find the page that holds the place */
        }
    }
    app_pos_note(a);
    return 0;
}

static void app_init(struct app *a, const struct app_mem *m, uint32_t pw, uint32_t ph, uint32_t rot, uint32_t look)
{
    a->rd.pw = pw; a->rd.ph = ph; a->rd.rot = rot;
    a->rd.text = m->text; a->rd.len = 0;
    rd_set_look(&a->rd, look);                            /* 0 = the plain look */
    a->chapter[0] = 0; a->chap_at = 0; a->chap_off = 0xFFFFFFFFu;
    a->m = *m;
    a->have_book = 0; a->title[0] = 0; a->note[0] = 0;
    a->page = 0; a->next_pos = 0;
    a->menu_sel = 0; a->light = 0; a->warmth = 0; a->asleep = 0;
    a->path[0] = 0;
    a->view = VIEW_FILES;
    a->book_path[0] = 0; a->book_hash = 0; a->book_stamp = 0; a->first_known = 0;
    pos_load(a);
    if (a->light) hal_set_light(a->light, a->warmth);
    app_load_dir(a);
    if (a->last_path[0]) {                                /* straight back into the book read last */
        const char *name = a->last_path;
        for (const char *c = a->last_path; *c; c++) if (*c == '/') name = c + 1;
        if (app_open(a, a->last_path, name)) a->note[0] = 0;   /* gone? then just show the library */
    }
}

/* ---------- painting ---------- */
#define LIST_TOP    190u
#define LIST_ROW    96u

static uint32_t app_rows(const struct app *a)
{
    return (rd_page_h(&a->rd) - LIST_TOP - 110u) / LIST_ROW;
}

/* A list screen: title, rows (left text, optional right text), a mark on the chosen row, hints.
 * How the chosen row is marked depends on the page look (rd.list). */
static void app_list_frame(const struct app *a, const char *title, const char *hint)
{
    const struct reader *r = &a->rd;
    uint32_t w = rd_page_w(r), h = rd_page_h(r), m = r->margin_x, tw = w - 2 * m;
    if (r->list == 2) {                                   /* classic: everything centred */
        rd_draw(r, r->head, m + (tw - rd_drawn_width(r->head, title, tw)) / 2, 44, title, tw);
        rd_rect(r, w / 2 - 60, 150, 120, 3, 15);
        if (a->note[0])
            rd_draw(r, r->small, m + (tw - rd_drawn_width(r->small, a->note, tw)) / 2, h - 130, a->note, tw);
        rd_draw(r, r->note, m + (tw - rd_drawn_width(r->note, hint, tw)) / 2, h - 70, hint, tw);
        return;
    }
    rd_draw(r, r->head, m, r->list == 3 ? 44 : 50, title, tw);
    if (r->list != 3)
        rd_rect(r, m, 150, tw, 4, 15);
    if (a->note[0])
        rd_draw(r, r->small, m, h - 130, a->note, tw);
    rd_draw(r, r->small, m, h - 70, hint, tw);
}
static void app_list_row(const struct app *a, uint32_t row, const char *left, const char *right, int chosen)
{
    struct reader pen = a->rd;                            /* a copy, so one row can be drawn white on black */
    const struct reader *r = &pen;
    uint32_t w = rd_page_w(r), m = r->margin_x, y = LIST_TOP + row * LIST_ROW;
    uint32_t rw = right ? rd_width(r->body, right) : 0;
    uint32_t pad = r->list == 2 ? 56 : r->list == 3 ? 44 : 30;
    if (r->list == 1 && chosen) {                         /* black bar, white text */
        rd_rect(r, m, y + 2, w - 2 * m, LIST_ROW - 8, 15);
        pen.invert = 1;
    }
    rd_draw(r, r->body, m + pad, y + 18, left, w - 2 * m - pad - 30 - (rw ? rw + 40 : 0));
    if (right)
        rd_draw(r, r->body, w - m - 30 - rw, y + 18, right, rw + 10);
    pen.invert = 0;
    if (r->list == 2) {                                   /* hairline under every row, a triangle at the chosen one */
        rd_rect(r, m, y + LIST_ROW - 5, w - 2 * m, 2, 5);
        if (chosen)
            for (uint32_t i = 0; i < 26; i++)
                rd_rect(r, m + 10, y + 34 + i, i < 13 ? 2 * i + 2 : 2 * (25 - i) + 2, 1, 15);
    }
    if (r->list == 3 && chosen)                           /* a thick bar beside the chosen row */
        rd_rect(r, m, y + 8, 12, LIST_ROW - 20, 15);
    if (r->list == 0 && chosen) {                         /* frame: 5 px lines */
        rd_rect(r, m, y, w - 2 * m, 5, 15);
        rd_rect(r, m, y + LIST_ROW - 9, w - 2 * m, 5, 15);
        rd_rect(r, m, y, 5, LIST_ROW - 4, 15);
        rd_rect(r, w - m - 5, y, 5, LIST_ROW - 4, 15);
    }
}

/* The chapter heading in force at text offset p, into a->chapter ("" when there is none).
 * Going forward it only looks at the text passed since last time. */
static void app_chapter(struct app *a, uint32_t p)
{
    const uint8_t *t = a->rd.text;
    uint32_t found = 0xFFFFFFFFu;
    if (p >= a->chap_at) {
        found = a->chap_off;
        for (uint32_t i = a->chap_at; i < p; i++)
            if (t[i] == RD_HEAD) found = i;
    } else {
        for (uint32_t i = p; i > 0; i--)
            if (t[i - 1] == RD_HEAD) { found = i - 1; break; }
    }
    a->chap_at = p;
    if (found == a->chap_off)
        return;
    a->chap_off = found;
    uint32_t n = 0;
    for (uint32_t i = found; found != 0xFFFFFFFFu && i < a->rd.len && t[i] != '\n' && n + 5 < sizeof a->chapter; i++)
        if (t[i] >= 32) a->chapter[n++] = (char)t[i];
    while (n > 0 && (a->chapter[n - 1] & 0xC0) == 0x80) n--;      /* never end in half a letter */
    if (n > 0 && (a->chapter[n - 1] & 0xC0) == 0xC0) n--;
    a->chapter[n] = 0;
}

static const char *const app_menu_items[] = { "Back to the book", "Library", "Light", "Warmth", "Leave Nihil" };
#define APP_MENU_N 5u
static const uint8_t app_levels[] = { 0, 5, 15, 30, 50, 75, 100 };

static void app_percent(char *b, uint32_t v)
{
    char t[12], *s = rd_utoa(v, t + sizeof t);
    uint32_t i = 0;
    if (v == 0) { app_copy(b, "off", 8); return; }
    for (; s[i]; i++) b[i] = s[i];
    b[i++] = '%'; b[i] = 0;
}

/* Paints the current screen into img (pw x ph bytes, must already be white). */
static void app_paint(struct app *a, volatile uint8_t *img)
{
    a->rd.img = img;
    if (a->view == VIEW_BOOK) {
        uint32_t p = a->m.pages[a->page];
        if (a->rd.header_h) {                             /* running header, but not on a page a chapter opens */
            uint32_t q = p;
            int head = 0, ni, it;
            while (q < a->rd.len && (a->rd.text[q] == '\n' || a->rd.text[q] == '\r' || a->rd.text[q] == RD_BREAK)) q++;
            if (q < a->rd.len) rd_style_at(&a->rd, q, &head, &ni, &it);
            if (!head) { app_chapter(a, p); rd_header(&a->rd, a->title, a->chapter); }
        }
        a->next_pos = rd_page(&a->rd, p, 1);
        rd_footer(&a->rd, a->page + 1, a->next_pos);
    } else if (a->view == VIEW_MENU) {
        app_list_frame(a, "Nihil", "Short press: move.  Hold lower button: choose.  Hold upper button: close.");
        for (uint32_t i = 0; i < APP_MENU_N; i++) {
            char v[12];
            const char *right = 0;
            if (i == 2) { app_percent(v, a->light); right = v; }
            if (i == 3) { app_percent(v, a->warmth); right = v; }
            app_list_row(a, i, app_menu_items[i], right, i == a->menu_sel);
        }
    } else {
        char title[APP_PATH_MAX + 16];
        app_copy(title, a->path[0] ? a->path : "Library", sizeof title);
        app_list_frame(a, title, "Short press: move.  Hold lower button: open.  Hold upper button: back.");
        uint32_t rows = app_rows(a);
        if (a->count == 0)
            rd_draw(&a->rd, a->rd.body, a->rd.margin_x + 30, LIST_TOP + 18, "No books in this folder.", 1000);
        for (uint32_t i = 0; i < rows && a->top + i < a->count; i++) {
            const struct app_entry *e = &a->entries[a->top + i];
            char shown[APP_NAME_MAX];
            app_copy(shown, e->name, sizeof shown);
            if (a->rd.list && !e->is_dir) {               /* the newer looks leave ".epub" / ".txt" off */
                uint32_t n = rd_strlen(shown);
                while (n > 1 && shown[n - 1] != '.') n--;
                if (n > 1) shown[n - 1] = 0;
            }
            app_list_row(a, i, shown, e->is_dir ? ">" : 0, a->top + i == a->sel);
        }
        if (a->count > rows) {                            /* scroll mark on the right edge */
            uint32_t h = rows * LIST_ROW, bar = h * rows / a->count, off = h * a->top / a->count;
            rd_rect(&a->rd, rd_page_w(&a->rd) - 24, LIST_TOP + off, 8, bar ? bar : 8, 15);
        }
    }
}

/* ---------- buttons ---------- */
static void app_step(uint32_t *v, int up)                 /* next / previous light level, wrapping */
{
    uint32_t n = sizeof app_levels, i = 0;
    while (i < n && app_levels[i] != *v) i++;
    if (i == n) i = 0;
    i = up ? (i + 1) % n : (i + n - 1) % n;
    *v = app_levels[i];
}

/* The cover was closed (1) or opened (0). Nothing needs repainting. */
static void app_cover(struct app *a, int closed)
{
    if (closed == a->asleep)
        return;
    a->asleep = closed;
    if (closed) hal_set_light(0, 0);
    else hal_set_light(a->light, a->warmth);
}

/* A button event. Returns APP_SAME, APP_REDRAW or APP_LEAVE. */
static int app_key(struct app *a, enum app_key k)
{
    if (a->asleep)
        return APP_SAME;                                  /* a button squeezed inside a closed cover does nothing */
    if (a->view == VIEW_BOOK) {
        if (k == KEY_NEXT) {
            if (a->next_pos >= a->rd.len || a->page + 1 >= a->m.pages_max)
                return APP_SAME;                          /* last page */
            a->page++;
            a->m.pages[a->page] = a->next_pos;
            app_pos_note(a);
            return APP_REDRAW;
        }
        if (k == KEY_BACK) {
            if (a->page == 0) return APP_SAME;
            if (a->page <= a->first_known)                /* earlier pages not worked out yet (book was reopened) */
                app_pages_upto(a, a->m.pages[a->page]);
            if (a->page == 0) return APP_REDRAW;
            a->page--;
            app_pos_note(a);
            return APP_REDRAW;
        }
        if (k == KEY_BACK_HOLD) { a->view = VIEW_MENU; a->menu_sel = 0; a->note[0] = 0; return APP_REDRAW; }
        return APP_SAME;
    }
    if (a->view == VIEW_MENU) {
        if (k == KEY_NEXT) { a->menu_sel = (a->menu_sel + 1) % APP_MENU_N; return APP_REDRAW; }
        if (k == KEY_BACK) { a->menu_sel = (a->menu_sel + APP_MENU_N - 1) % APP_MENU_N; return APP_REDRAW; }
        if (k == KEY_BACK_HOLD || (k == KEY_NEXT_HOLD && a->menu_sel == 0)) {
            if (!a->have_book) { a->view = VIEW_FILES; return APP_REDRAW; }
            a->view = VIEW_BOOK;
            return APP_REDRAW;
        }
        if (a->menu_sel == 1) { a->view = VIEW_FILES; a->note[0] = 0; app_load_dir(a); return APP_REDRAW; }
        if (a->menu_sel == 2) { app_step(&a->light, 1); hal_set_light(a->light, a->warmth); app_pos_note(a); return APP_REDRAW; }
        if (a->menu_sel == 3) { app_step(&a->warmth, 1); hal_set_light(a->light, a->warmth); app_pos_note(a); return APP_REDRAW; }
        return APP_LEAVE;
    }
    /* file list */
    uint32_t rows = app_rows(a);
    if (k == KEY_NEXT || k == KEY_BACK) {
        if (a->count == 0) return APP_SAME;
        a->sel = k == KEY_NEXT ? (a->sel + 1) % a->count : (a->sel + a->count - 1) % a->count;
        if (a->sel < a->top) a->top = a->sel;
        if (a->sel >= a->top + rows) a->top = a->sel - rows + 1;
        return APP_REDRAW;
    }
    if (k == KEY_BACK_HOLD) {                             /* up one folder, or to the menu from the top */
        a->note[0] = 0;
        uint32_t n = rd_strlen(a->path);
        if (n == 0) { a->view = VIEW_MENU; a->menu_sel = a->have_book ? 0 : 1; return APP_REDRAW; }
        while (n > 0 && a->path[n - 1] != '/') n--;
        a->path[n ? n - 1 : 0] = 0;
        app_load_dir(a);
        return APP_REDRAW;
    }
    if (a->count == 0) return APP_SAME;
    const struct app_entry *e = &a->entries[a->sel];
    char full[APP_PATH_MAX];
    app_copy(full, a->path, sizeof full);
    if (full[0]) app_cat(full, "/", sizeof full);
    app_cat(full, e->name, sizeof full);
    if (e->is_dir) {
        app_copy(a->path, full, sizeof a->path);
        a->note[0] = 0;
        app_load_dir(a);
        return APP_REDRAW;
    }
    app_open(a, full, e->name);                           /* on failure the list stays, with a note */
    return APP_REDRAW;
}
