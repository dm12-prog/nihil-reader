/* Tiny drawing helpers for an 8-bit picture (0 = black, 255 = white).
 * Kept free of hardware access so the same code can be tried on a PC. */
#pragma once
#include <stdint.h>

/* Classic 5x7 dot font, one byte per column, lowest bit = top row. */
static const uint8_t font5x7[36][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};

/* The picture in memory is always the panel's own shape (pw x ph, landscape
 * on the Sage). Drawing uses "page" coordinates; draw_rot says how the page
 * sits on the panel: 0 = same way, 1 = page top at the panel's right edge,
 * 2 = upside down, 3 = page top at the panel's left edge. */
static uint32_t draw_rot;
static uint32_t page_w(uint32_t pw, uint32_t ph) { return (draw_rot & 1) ? ph : pw; }
static uint32_t page_h(uint32_t pw, uint32_t ph) { return (draw_rot & 1) ? pw : ph; }

static void draw_rect(volatile uint8_t *img, uint32_t pw, uint32_t ph, uint32_t x0, uint32_t y0, uint32_t rw, uint32_t rh, uint8_t c)
{
    uint32_t w = page_w(pw, ph), h = page_h(pw, ph);
    for (uint32_t y = y0; y < y0 + rh && y < h; y++)
        for (uint32_t x = x0; x < x0 + rw && x < w; x++) {
            uint32_t X, Y;
            switch (draw_rot) {
            case 1:  X = pw - 1 - y; Y = x;          break;
            case 2:  X = pw - 1 - x; Y = ph - 1 - y; break;
            case 3:  X = y;          Y = ph - 1 - x; break;
            default: X = x;          Y = y;          break;
            }
            img[Y * pw + X] = c;
        }
}

/* Draws digits, capital letters and spaces; returns the x after the text. */
static uint32_t draw_text(volatile uint8_t *img, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t scale, const char *s)
{
    for (; *s; s++, x += 6 * scale) {
        int g = *s >= '0' && *s <= '9' ? *s - '0' : *s >= 'A' && *s <= 'Z' ? *s - 'A' + 10 : -1;
        if (g < 0)
            continue;
        for (uint32_t col = 0; col < 5; col++)
            for (uint32_t row = 0; row < 7; row++)
                if (font5x7[g][col] & (1u << row))
                    draw_rect(img, w, h, x + col * scale, y + row * scale, scale, scale, 0x00);
    }
    return x;
}

static void draw_number(volatile uint8_t *img, uint32_t w, uint32_t h, uint32_t x, uint32_t y, uint32_t scale, uint32_t n)
{
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + n % 10); n /= 10; } while (n && i > 0);
    draw_text(img, w, h, x, y, scale, &b[i]);
}

/* One "page" of the test book (laid out for any page shape). */
static void draw_page(volatile uint8_t *img, uint32_t pw, uint32_t ph, uint32_t page)
{
    static const char *const body[4] = {
        "THE QUICK BROWN FOX JUMPS", "OVER THE LAZY DOG", "NO LINUX IS RUNNING NOW", "HOLD BOTH BUTTONS TO LEAVE",
    };
    uint32_t w = page_w(pw, ph), h = page_h(pw, ph), m = 120;
    draw_text(img, pw, ph, m, 100, 24, "NIHIL");
    draw_rect(img, pw, ph, m, 300, w - 2 * m, 8, 0x00);
    uint32_t x = draw_text(img, pw, ph, m, 380, 16, "PAGE ");
    draw_number(img, pw, ph, x, 380, 16, page);
    uint32_t digits = page >= 100 ? 3 : page >= 10 ? 2 : 1, sc = h > w ? 60 : 50;
    draw_number(img, pw, ph, (w - (digits * 6 - 1) * sc) / 2, h > w ? 640 : 520 + 0 * m, sc, page);
    uint32_t by = h > w ? 1240 : 940;
    for (uint32_t i = 0; i < 4; i++)
        draw_text(img, pw, ph, m, by + i * 90, 8, body[(i + page) % 4]);
    /* progress bar: one block per page, wraps at 20 */
    draw_rect(img, pw, ph, m, h - 100, w - 2 * m, 4, 0x00);
    draw_rect(img, pw, ph, m, h - 130, ((w - 2 * m) / 20) * ((page - 1) % 20 + 1), 30, 0x00);
}
