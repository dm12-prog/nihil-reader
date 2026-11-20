/*
 * Nihil app: what the reader does with a book and two buttons.
 * Hardware-free. The Sage build (platform/sage/main.c) and the PC simulator
 * (tools/sim) both drive this same code, so what the simulator shows is what
 * the device will draw.
 */
#pragma once
#include "reader.h"

enum app_key { KEY_NEXT, KEY_BACK };

struct app {
    struct reader rd;
    volatile uint32_t *page_start;   /* text offset where each page begins */
    uint32_t page_max;               /* room in page_start[] */
    uint32_t page;                   /* current page, 0 = first */
    uint32_t next_pos;               /* text offset just after the current page */
};

static void app_init(struct app *a, const uint8_t *text, uint32_t len, uint32_t pw, uint32_t ph, uint32_t rot,
                     volatile uint32_t *page_table, uint32_t page_max)
{
    a->rd.pw = pw; a->rd.ph = ph; a->rd.rot = rot;
    a->rd.text = text; a->rd.len = len;
    rd_defaults(&a->rd);
    a->page_start = page_table; a->page_max = page_max;
    a->page = 0; a->page_start[0] = 0; a->next_pos = 0;
}

/* Paints the current page into img (pw x ph bytes, must already be white). */
static void app_paint(struct app *a, volatile uint8_t *img)
{
    a->rd.img = img;
    a->next_pos = rd_page(&a->rd, a->page_start[a->page], 1);
    rd_footer(&a->rd, a->page + 1, a->next_pos);
}

/* A button was pressed. Returns 1 if the page changed and must be painted again. */
static int app_key(struct app *a, enum app_key k)
{
    if (k == KEY_NEXT) {
        if (a->next_pos >= a->rd.len || a->page + 1 >= a->page_max)
            return 0;                                         /* last page */
        a->page++;
        a->page_start[a->page] = a->next_pos;
        return 1;
    }
    if (a->page == 0)
        return 0;
    a->page--;
    return 1;
}
