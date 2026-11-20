/* The few C library routines the compiler may call on its own (struct copies, clearing). */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b) while (n--) *a++ = *b++;
    else while (n--) a[n] = b[n];
    return d;
}
void *memset(void *d, int c, size_t n)
{
    uint8_t *a = d;
    while (n--) *a++ = (uint8_t)c;
    return d;
}

/* The same routines under the names the ARM rule book (AEABI, "Run-time ABI
 * for the Arm Architecture", section 4.3.4) lets the compiler use. Note the
 * different argument order of __aeabi_memset: (dest, n, c). */
void __aeabi_memcpy(void *d, const void *s, size_t n)   { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n)  { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n)  { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n)  { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c)           { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c)          { memset(d, c, n); }
void __aeabi_memset8(void *d, size_t n, int c)          { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n)                  { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n)                 { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n)                 { memset(d, 0, n); }
