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
    a->page = 0; a->m.pages[0] = 0; a->next_pos = 0;
    a->have_book = 1;
    a->view = VIEW_BOOK;
    return 0;
}

static void app_init(struct app *a, const struct app_mem *m, uint32_t pw, uint32_t ph, uint32_t rot)
{
    a->rd.pw = pw; a->rd.ph = ph; a->rd.rot = rot;
    a->rd.text = m->text; a->rd.len = 0;
    rd_defaults(&a->rd);
    a->m = *m;
    a->have_book = 0; a->title[0] = 0; a->note[0] = 0;
    a->page = 0; a->next_pos = 0;
    a->menu_sel = 0; a->light = 0; a->warmth = 0; a->asleep = 0;
    a->path[0] = 0;
    a->view = VIEW_FILES;
    app_load_dir(a);
}

/* ---------- painting ---------- */
#define LIST_TOP    190u
#define LIST_ROW    96u

static uint32_t app_rows(const struct app *a)
{
    return (rd_page_h(&a->rd) - LIST_TOP - 110u) / LIST_ROW;
}

/* A list screen: title, rows (left text, optional right text), a frame around the chosen row, hints. */
static void app_list_frame(const struct app *a, const char *title, const char *hint)
{
    const struct reader *r = &a->rd;
    uint32_t w = rd_page_w(r), h = rd_page_h(r), m = r->margin_x;
    rd_draw(r, r->head, m, 50, title, w - 2 * m);
    rd_rect(r, m, 150, w - 2 * m, 4, 15);
    if (a->note[0])
        rd_draw(r, r->small, m, h - 130, a->note, w - 2 * m);
    rd_draw(r, r->small, m, h - 70, hint, w - 2 * m);
}
static void app_list_row(const struct app *a, uint32_t row, const char *left, const char *right, int chosen)
{
    const struct reader *r = &a->rd;
    uint32_t w = rd_page_w(r), m = r->margin_x, y = LIST_TOP + row * LIST_ROW;
    uint32_t rw = right ? rd_width(r->body, right) : 0;
    rd_draw(r, r->body, m + 30, y + 18, left, w - 2 * m - 60 - (rw ? rw + 40 : 0));
    if (right)
        rd_draw(r, r->body, w - m - 30 - rw, y + 18, right, rw + 10);
    if (chosen) {                                         /* frame: 5 px lines */
        rd_rect(r, m, y, w - 2 * m, 5, 15);
        rd_rect(r, m, y + LIST_ROW - 9, w - 2 * m, 5, 15);
        rd_rect(r, m, y, 5, LIST_ROW - 4, 15);
        rd_rect(r, w - m - 5, y, 5, LIST_ROW - 4, 15);
    }
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
        a->next_pos = rd_page(&a->rd, a->m.pages[a->page], 1);
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
            app_list_row(a, i, e->name, e->is_dir ? ">" : 0, a->top + i == a->sel);
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
            return APP_REDRAW;
        }
        if (k == KEY_BACK) {
            if (a->page == 0) return APP_SAME;
            a->page--;
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
        if (a->menu_sel == 2) { app_step(&a->light, 1); hal_set_light(a->light, a->warmth); return APP_REDRAW; }
        if (a->menu_sel == 3) { app_step(&a->warmth, 1); hal_set_light(a->light, a->warmth); return APP_REDRAW; }
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
