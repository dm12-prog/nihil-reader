/*
 * The book library on a FAT32 volume: lists folders and books, reads a book.
 * Shared by every platform that reaches its storage through hal_blk_read().
 */
#pragma once
#include "fat.h"
#include "app.h"

static struct fat lib_fat;
static int lib_ready;

static int lib_mount(void)
{
    lib_ready = fat_mount(&lib_fat) == 0;
    return lib_ready ? 0 : -1;
}

static int lib_is_book(const char *n)
{
    uint32_t l = rd_strlen(n);
    const char *e5 = l > 5 ? n + l - 5 : "", *e4 = l > 4 ? n + l - 4 : "";
    return ep_eq((const uint8_t *)e5, 5, ".epub") || ep_eq((const uint8_t *)e4, 4, ".txt");
}

static int lib_list_dir(const char *path, struct app_entry *out, int max)
{
    static struct fat_entry e;
    static struct fat_dir d;
    if (!lib_ready || fat_find(&lib_fat, path, &e) || !e.is_dir) return -1;
    fat_dir_open(&lib_fat, &d, e.clus);
    int n = 0;
    while (n < max && fat_dir_next(&lib_fat, &d, &e)) {
        if (e.name[0] == '.') continue;                   /* hidden: .kobo, .adds, macOS "._" files */
        if (!e.is_dir && !lib_is_book(e.name)) continue;
        app_copy(out[n].name, e.name, sizeof out[n].name);
        out[n].is_dir = e.is_dir;
        n++;
    }
    return n;
}

static int lib_read_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *len)
{
    static struct fat_entry e;
    if (!lib_ready || fat_find(&lib_fat, path, &e)) return -1;
    return fat_read(&lib_fat, &e, buf, max, len);
}
