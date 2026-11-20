/*
 * Read-only FAT32 with long file names, on top of "read N sectors" from the
 * platform. This is how Nihil will see the Kobo's book storage (the part that
 * shows up as the KOBOeReader drive on a computer).
 *
 * On-disk layout per Microsoft's "FAT32 File System Specification" (fatgen103)
 * and, for finding the partition, the classic MBR table and the UEFI GPT.
 * Nothing in here ever writes.
 */
#pragma once
#include <stdint.h>

/* Platform service: read `count` 512-byte sectors starting at `lba`. Returns 0 when ok. */
static int hal_blk_read(uint32_t lba, uint32_t count, uint8_t *buf);

#define FAT_NAME_MAX 384

struct fat {
    uint32_t part_lba, spc, fat_lba, data_lba, root_clus, clusters;
    uint8_t sec[512]; uint32_t sec_lba; int sec_ok;       /* one cached sector */
};
struct fat_entry { char name[FAT_NAME_MAX]; uint32_t clus, size; uint8_t is_dir; };
struct fat_dir {
    uint32_t clus, sector, index;                         /* where we are in the folder */
    uint16_t lfn[260]; uint32_t lfn_len; uint8_t lfn_sum; int lfn_ok;
    int done;
};

static uint32_t fat_u16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t fat_u32(const uint8_t *p) { return fat_u16(p) | fat_u16(p + 2) << 16; }

static const uint8_t *fat_sector(struct fat *f, uint32_t lba)
{
    if (!f->sec_ok || f->sec_lba != lba) {
        f->sec_ok = 0;
        if (hal_blk_read(lba, 1, f->sec)) return 0;
        f->sec_lba = lba; f->sec_ok = 1;
    }
    return f->sec;
}

static int fat_try(struct fat *f, uint32_t lba)           /* is there a FAT32 volume at this sector? */
{
    const uint8_t *b = fat_sector(f, lba);
    if (!b || b[510] != 0x55 || b[511] != 0xAA) return -1;
    if (fat_u16(b + 11) != 512 || b[13] == 0 || fat_u16(b + 22) != 0 || fat_u32(b + 36) == 0) return -1;
    uint32_t reserved = fat_u16(b + 14), nfats = b[16], fatsz = fat_u32(b + 36), total = fat_u32(b + 32);
    f->part_lba = lba; f->spc = b[13];
    f->fat_lba = lba + reserved;
    f->data_lba = f->fat_lba + nfats * fatsz;
    f->root_clus = fat_u32(b + 44);
    f->clusters = (total - reserved - nfats * fatsz) / f->spc;
    return f->clusters >= 65525u ? 0 : -1;                /* fewer clusters would be FAT16 */
}

/* Finds the FAT32 volume: whole disk, an MBR partition, or a GPT partition. Returns 0 when found. */
static int fat_mount(struct fat *f)
{
    f->sec_ok = 0;
    if (fat_try(f, 0) == 0) return 0;
    const uint8_t *b = fat_sector(f, 0);
    if (!b) return -1;
    uint32_t starts[4];
    uint8_t types[4];
    for (int i = 0; i < 4; i++) { types[i] = b[446 + i * 16 + 4]; starts[i] = fat_u32(b + 446 + i * 16 + 8); }
    for (int i = 0; i < 4; i++)
        if (types[i] != 0 && types[i] != 0xEE && starts[i] && fat_try(f, starts[i]) == 0) return 0;
    /* GPT: header in sector 1, entries usually from sector 2, 128 bytes each */
    b = fat_sector(f, 1);
    if (!b || fat_u32(b) != 0x20494645u || fat_u32(b + 4) != 0x54524150u) return -1;   /* "EFI PART" */
    uint32_t ent_lba = fat_u32(b + 72), n = fat_u32(b + 80), esz = fat_u32(b + 84);
    if (esz < 128 || esz > 512 || n > 256) return -1;
    for (uint32_t i = 0; i < n; i++) {
        b = fat_sector(f, ent_lba + i * esz / 512);
        if (!b) return -1;
        const uint8_t *e = b + (i * esz) % 512;
        uint32_t first = fat_u32(e + 32), first_hi = fat_u32(e + 36);
        if ((fat_u32(e) | fat_u32(e + 4) | fat_u32(e + 8) | fat_u32(e + 12)) == 0 || first_hi) continue;
        if (fat_try(f, first) == 0) return 0;
    }
    return -1;
}

static uint32_t fat_next(struct fat *f, uint32_t clus)    /* next cluster in a chain; 0 = end or error */
{
    const uint8_t *b = fat_sector(f, f->fat_lba + clus / 128);
    if (!b) return 0;
    uint32_t v = fat_u32(b + (clus % 128) * 4) & 0x0FFFFFFFu;
    return (v < 2 || v >= 0x0FFFFFF8u || v >= f->clusters + 2) ? 0 : v;
}
static uint32_t fat_clus_lba(const struct fat *f, uint32_t clus) { return f->data_lba + (clus - 2) * f->spc; }

static void fat_dir_open(struct fat *f, struct fat_dir *d, uint32_t clus)
{
    d->clus = clus ? clus : f->root_clus; d->sector = 0; d->index = 0;
    d->lfn_ok = 0; d->lfn_len = 0; d->done = 0;
}

static uint32_t fat_utf8(char *o, uint32_t n, uint32_t max, uint32_t c)
{
    if (c < 0x80) { if (n + 1 < max) o[n++] = (char)c; }
    else if (c < 0x800) { if (n + 2 < max) { o[n++] = (char)(0xC0 | c >> 6); o[n++] = (char)(0x80 | (c & 63)); } }
    else if (c < 0x10000) { if (n + 3 < max) { o[n++] = (char)(0xE0 | c >> 12); o[n++] = (char)(0x80 | (c >> 6 & 63)); o[n++] = (char)(0x80 | (c & 63)); } }
    else if (n + 4 < max) { o[n++] = (char)(0xF0 | c >> 18); o[n++] = (char)(0x80 | (c >> 12 & 63)); o[n++] = (char)(0x80 | (c >> 6 & 63)); o[n++] = (char)(0x80 | (c & 63)); }
    return n;
}

/* Next file or folder in the folder. Returns 1 and fills *e, or 0 at the end.
 * Skips deleted entries, the volume label, "." and "..". */
static int fat_dir_next(struct fat *f, struct fat_dir *d, struct fat_entry *e)
{
    while (!d->done) {
        if (d->index == 16) {
            d->index = 0;
            if (++d->sector == f->spc) {
                d->sector = 0;
                d->clus = fat_next(f, d->clus);
                if (!d->clus) break;
            }
        }
        const uint8_t *b = fat_sector(f, fat_clus_lba(f, d->clus) + d->sector);
        if (!b) break;
        const uint8_t *r = b + d->index++ * 32;
        if (r[0] == 0x00) break;
        if (r[0] == 0xE5) { d->lfn_ok = 0; continue; }
        if ((r[11] & 0x3F) == 0x0F) {                     /* a piece of a long name */
            uint32_t seq = r[0] & 0x1F;
            if (seq < 1 || seq > 20) { d->lfn_ok = 0; continue; }
            if (r[0] & 0x40) {
                d->lfn_ok = 1; d->lfn_sum = r[13]; d->lfn_len = seq * 13;
                for (uint32_t i = 0; i < 260; i++) d->lfn[i] = 0xFFFF;
            } else if (!d->lfn_ok || r[13] != d->lfn_sum) { d->lfn_ok = 0; continue; }
            static const uint8_t at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
            for (uint32_t i = 0; i < 13; i++)
                d->lfn[(seq - 1) * 13 + i] = (uint16_t)fat_u16(r + at[i]);
            continue;
        }
        int use_lfn = d->lfn_ok;
        d->lfn_ok = 0;
        if (r[11] & 0x08) continue;                       /* volume label */
        uint8_t sum = 0;
        for (int i = 0; i < 11; i++) sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + r[i]);
        uint32_t n = 0;
        if (use_lfn && sum == d->lfn_sum) {
            for (uint32_t i = 0; i < d->lfn_len && d->lfn[i] != 0 && d->lfn[i] != 0xFFFF; i++) {
                uint32_t c = d->lfn[i];
                if (c >= 0xD800 && c < 0xDC00 && i + 1 < d->lfn_len && d->lfn[i + 1] >= 0xDC00 && d->lfn[i + 1] < 0xE000) {
                    c = 0x10000u + ((c - 0xD800u) << 10) + (d->lfn[i + 1] - 0xDC00u);
                    i++;
                }
                n = fat_utf8(e->name, n, FAT_NAME_MAX, c);
            }
        } else {                                          /* short 8.3 name */
            for (int i = 0; i < 8 && r[i] != ' '; i++) {
                uint8_t c = r[i] == 0x05 ? 0xE5 : r[i];
                if ((r[12] & 0x08) && c >= 'A' && c <= 'Z') c += 32;
                n = fat_utf8(e->name, n, FAT_NAME_MAX, c < 0x80 ? c : '_');
            }
            if (r[8] != ' ') {
                n = fat_utf8(e->name, n, FAT_NAME_MAX, '.');
                for (int i = 8; i < 11 && r[i] != ' '; i++) {
                    uint8_t c = r[i];
                    if ((r[12] & 0x10) && c >= 'A' && c <= 'Z') c += 32;
                    n = fat_utf8(e->name, n, FAT_NAME_MAX, c < 0x80 ? c : '_');
                }
            }
        }
        e->name[n] = 0;
        if (e->name[0] == '.' && (e->name[1] == 0 || (e->name[1] == '.' && e->name[2] == 0))) continue;
        e->is_dir = (r[11] & 0x10) != 0;
        e->clus = fat_u16(r + 20) << 16 | fat_u16(r + 26);
        e->size = fat_u32(r + 28);
        return 1;
    }
    d->done = 1;
    return 0;
}

/* Finds "Folder/Sub/file.epub" (exact names; "" = the top folder). Returns 0 when found. */
static int fat_find(struct fat *f, const char *path, struct fat_entry *e)
{
    uint32_t clus = f->root_clus;
    e->name[0] = 0; e->clus = clus; e->size = 0; e->is_dir = 1;
    while (*path) {
        uint32_t n = 0;
        while (path[n] && path[n] != '/') n++;
        if (n) {
            if (!e->is_dir) return -1;
            static struct fat_dir d;
            fat_dir_open(f, &d, clus);
            int found = 0;
            while (!found && fat_dir_next(f, &d, e)) {
                uint32_t k = 0;
                while (k < n && e->name[k] == path[k]) k++;
                found = k == n && e->name[k] == 0;
            }
            if (!found) return -1;
            clus = e->clus;
        }
        path += n;
        if (*path == '/') path++;
    }
    return 0;
}

/* Reads a whole file. Returns 0 when ok and sets *len. Consecutive clusters are read in one go. */
static int fat_read(struct fat *f, const struct fat_entry *e, uint8_t *buf, uint32_t max, uint32_t *len)
{
    if (e->is_dir || e->size > max) return -1;
    uint32_t left = (e->size + 511u) / 512u, clus = e->clus, done = 0;
    uint8_t tail[512];
    while (left && clus) {
        uint32_t run = 1, last = clus;
        while (run * f->spc < left && run < 1024u) {      /* extend the run while the chain is contiguous */
            uint32_t nx = fat_next(f, last);
            if (nx != last + 1) break;
            last = nx; run++;
        }
        uint32_t secs = run * f->spc;
        if (secs > left) secs = left;
        uint32_t whole = secs;
        if (done + whole * 512u > max) whole = (max - done) / 512u;   /* last sector may not fit: go through tail[] */
        if (whole && hal_blk_read(fat_clus_lba(f, clus), whole, buf + done)) return -1;
        done += whole * 512u;
        if (whole < secs) {
            if (hal_blk_read(fat_clus_lba(f, clus) + whole, 1, tail)) return -1;
            for (uint32_t i = 0; done + i < e->size && i < 512; i++) buf[done + i] = tail[i];
            done = e->size;
        }
        left -= secs;
        clus = fat_next(f, last);
    }
    if (left) return -1;
    *len = e->size;
    return 0;
}
