/*
 * Nihil simulator: runs the reader on a PC and writes what the screen would
 * show after each button press.
 *
 *   sim <library folder> <keys> <out prefix> [rot]
 *     keys: n = lower button (next), b = upper button (back),
 *           N = hold lower button, B = hold upper button
 *     writes <prefix>-000.pgm (first screen), <prefix>-001.pgm (after key 1) ...
 *     Pictures are written upright (the way the reader holds the device).
 *
 * It simulates the screen's size, the two buttons and a folder of books.
 * It does NOT simulate the Sage's hardware (e-ink waveforms, power chip, storage).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../core/app.h"

#define PW 1920
#define PH 1440
static uint8_t img[PW * PH];
static const char *root;

static int is_book(const char *n)
{
    size_t l = strlen(n);
    return (l > 5 && !strcasecmp(n + l - 5, ".epub")) || (l > 4 && !strcasecmp(n + l - 4, ".txt"));
}
static int hal_list_dir(const char *path, struct app_entry *out, int max)
{
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", root, path);
    DIR *d = opendir(full);
    if (!d) return -1;
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) && n < max;) {
        char p[4096];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", full, e->d_name);
        if (stat(p, &st)) continue;
        if (!S_ISDIR(st.st_mode) && !is_book(e->d_name)) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode);
        n++;
    }
    closedir(d);
    return n;
}
static int hal_read_file(const char *path, uint8_t *buf, uint32_t max, uint32_t *len)
{
    char full[2048];
    snprintf(full, sizeof full, "%s/%s", root, path);
    FILE *f = fopen(full, "rb");
    if (!f) return -1;
    *len = (uint32_t)fread(buf, 1, max, f);
    fclose(f);
    return 0;
}
static void hal_busy(void) {}
static void hal_set_light(uint32_t brightness, uint32_t warmth)
{
    printf("light: brightness %u%%, warmth %u%%\n", brightness, warmth);
}

static void save(const struct app *a, const char *prefix, int n, char key)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%03d.pgm", prefix, n);
    FILE *f = fopen(name, "wb");
    if (!f) { perror(name); exit(1); }
    uint32_t w = rd_page_w(&a->rd), h = rd_page_h(&a->rd);
    fprintf(f, "P5\n%u %u\n255\n", w, h);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            uint32_t X, Y;
            switch (a->rd.rot) {
            case 1:  X = PW - 1 - y; Y = x;          break;
            case 2:  X = PW - 1 - x; Y = PH - 1 - y; break;
            case 3:  X = y;          Y = PH - 1 - x; break;
            default: X = x;          Y = y;          break;
            }
            fputc(img[Y * PW + X], f);
        }
    fclose(f);
    static const char *const views[] = { "book", "menu", "files" };
    printf("%s: key '%c' -> %s", name, key, views[a->view]);
    if (a->view == VIEW_BOOK) printf(" \"%s\" page %u", a->title, a->page + 1);
    if (a->view == VIEW_FILES) printf(" \"%s\" %u entries, chosen %u", a->path, a->count, a->sel);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: sim <library folder> <keys> <out prefix> [rot]\n"); return 1; }
    root = argv[1];
    static uint8_t file[64 << 20], work[32 << 20], text[64 << 20];
    static uint32_t pages[1 << 20];
    static struct app a;
    struct app_mem m = { file, sizeof file, work, sizeof work, text, sizeof text, pages, 1 << 20 };
    app_init(&a, &m, PW, PH, argc > 4 ? (uint32_t)atoi(argv[4]) : 1);
    memset(img, 255, sizeof img);
    app_paint(&a, img);
    save(&a, argv[3], 0, '-');
    int n = 1;
    for (const char *k = argv[2]; *k; k++, n++) {
        enum app_key key = *k == 'n' ? KEY_NEXT : *k == 'b' ? KEY_BACK : *k == 'N' ? KEY_NEXT_HOLD : KEY_BACK_HOLD;
        int r = app_key(&a, key);
        if (r == APP_LEAVE) { printf("leave requested\n"); break; }
        if (r == APP_REDRAW) {
            memset(img, 255, sizeof img);
            app_paint(&a, img);
        }
        save(&a, argv[3], n, *k);
    }
    return 0;
}
