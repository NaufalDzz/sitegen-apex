#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__)
#define __MM_MALLOC_H
#include <immintrin.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#endif

// ---- Freestanding Linux Syscalls ----
#define AT_FDCWD (-100)
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
#define SEEK_SET 0
#define SEEK_END 2
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

#if defined(__x86_64__)
static inline long sys_sc6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a3;
    register long r8  __asm__("r8")  = a4;
    register long r9  __asm__("r9")  = a5;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}
#define SYS_read       0
#define SYS_write      1
#define SYS_close      3
#define SYS_lseek      8
#define SYS_mmap       9
#define SYS_munmap     11
#define SYS_exit_group 231
#define SYS_openat     257
#elif defined(__aarch64__)
static inline long sys_sc6(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a0;
    register long x1 __asm__("x1") = a1;
    register long x2 __asm__("x2") = a2;
    register long x3 __asm__("x3") = a3;
    register long x4 __asm__("x4") = a4;
    register long x5 __asm__("x5") = a5;
    __asm__ volatile("svc #0"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory");
    return x0;
}
#define SYS_openat     56
#define SYS_close      57
#define SYS_lseek      62
#define SYS_read       63
#define SYS_write      64
#define SYS_exit_group 94
#define SYS_munmap     215
#define SYS_mmap       222
#endif

static inline long sys_openat(int dirfd, const char *path, int flags, int mode) {
    return sys_sc6(SYS_openat, dirfd, (long)path, flags, mode, 0, 0);
}
static inline long sys_read(int fd, void *buf, size_t len) {
    return sys_sc6(SYS_read, fd, (long)buf, (long)len, 0, 0, 0);
}
static inline long sys_write(int fd, const void *buf, size_t len) {
    return sys_sc6(SYS_write, fd, (long)buf, (long)len, 0, 0, 0);
}
static inline long sys_close(int fd) {
    return sys_sc6(SYS_close, fd, 0, 0, 0, 0, 0);
}
static inline long sys_lseek(int fd, long off, int whence) {
    return sys_sc6(SYS_lseek, fd, off, whence, 0, 0, 0);
}
static inline void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    return (void *)sys_sc6(SYS_mmap, (long)addr, (long)len, prot, flags, fd, off);
}
static inline void sys_exit_group(int code) {
    sys_sc6(SYS_exit_group, code, 0, 0, 0, 0, 0);
    for (;;) {}
}

// ---- Freestanding memory intrinsics ----
#ifndef STANDALONE_CLI
void *memcpy(void *restrict dst, const void *restrict src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
#if defined(__aarch64__)
    while (n >= 64) {
        uint8x16_t a = vld1q_u8(s);
        uint8x16_t b = vld1q_u8(s + 16);
        uint8x16_t c = vld1q_u8(s + 32);
        uint8x16_t e = vld1q_u8(s + 48);
        vst1q_u8(d, a);
        vst1q_u8(d + 16, b);
        vst1q_u8(d + 32, c);
        vst1q_u8(d + 48, e);
        d += 64; s += 64; n -= 64;
    }
    while (n >= 16) {
        vst1q_u8(d, vld1q_u8(s));
        d += 16; s += 16; n -= 16;
    }
#elif defined(__x86_64__)
    while (n >= 64) {
        __m128i a = _mm_loadu_si128((const __m128i *)s);
        __m128i b = _mm_loadu_si128((const __m128i *)(s + 16));
        __m128i c = _mm_loadu_si128((const __m128i *)(s + 32));
        __m128i e = _mm_loadu_si128((const __m128i *)(s + 48));
        _mm_storeu_si128((__m128i *)d, a);
        _mm_storeu_si128((__m128i *)(d + 16), b);
        _mm_storeu_si128((__m128i *)(d + 32), c);
        _mm_storeu_si128((__m128i *)(d + 48), e);
        d += 64; s += 64; n -= 64;
    }
    while (n >= 16) {
        _mm_storeu_si128((__m128i *)d, _mm_loadu_si128((const __m128i *)s));
        d += 16; s += 16; n -= 16;
    }
#endif
    while (n >= 8) {
        uint64_t v;
        __builtin_memcpy(&v, s, 8);
        __builtin_memcpy(d, &v, 8);
        d += 8; s += 8; n -= 8;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0) return dst;
    if (d < s || d >= s + n) return memcpy(dst, src, n);
    d += n; s += n;
    while (n--) *--d = *--s;
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    uint8_t b = (uint8_t)c;
    uint64_t v = (uint64_t)b * 0x0101010101010101ULL;
    while (n >= 8) {
        __builtin_memcpy(d, &v, 8);
        d += 8; n -= 8;
    }
    while (n--) *d++ = b;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = (const uint8_t *)a;
    const uint8_t *q = (const uint8_t *)b;
    while (n >= 8) {
        uint64_t x, y;
        __builtin_memcpy(&x, p, 8);
        __builtin_memcpy(&y, q, 8);
        if (x != y) break;
        p += 8; q += 8; n -= 8;
    }
    while (n--) {
        if (*p != *q) return (int)*p - (int)*q;
        p++; q++;
    }
    return 0;
}

int bcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}
#else
#include <string.h>
#endif

static inline void fast_copy(void *restrict dst, const void *restrict src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (n <= 16) {
        if (n >= 8) {
            uint64_t a, b;
            __builtin_memcpy(&a, s, 8);
            __builtin_memcpy(&b, s + n - 8, 8);
            __builtin_memcpy(d, &a, 8);
            __builtin_memcpy(d + n - 8, &b, 8);
            return;
        }
        if (n >= 4) {
            uint32_t a, b;
            __builtin_memcpy(&a, s, 4);
            __builtin_memcpy(&b, s + n - 4, 4);
            __builtin_memcpy(d, &a, 4);
            __builtin_memcpy(d + n - 4, &b, 4);
            return;
        }
        if (n > 0) {
            d[0] = s[0];
            d[n >> 1] = s[n >> 1];
            d[n - 1] = s[n - 1];
        }
        return;
    }
    memcpy(d, s, n);
}

// ---- Str slice & Bump Arena & Output Buffer ----
typedef struct {
    const char *p;
    uint32_t len;
} Str;

#define STR_LIT(s) ((Str){ (s), (uint32_t)(sizeof(s) - 1) })
#define STR_EMPTY ((Str){ "", 0 })

typedef struct {
    uint8_t *base;
    size_t pos;
    size_t cap;
} Arena;

static inline void *arena_alloc(Arena *a, size_t n, size_t align) {
    size_t p = (a->pos + (align - 1)) & ~(align - 1);
    void *res = a->base + p;
    a->pos = p + n;
    return res;
}

static inline char *arena_alloc_str(Arena *a, size_t n) {
    char *res = (char *)(a->base + a->pos);
    a->pos += n;
    return res;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int fd; // >= 0 if stream-flushable
} Buf;

static inline void buf_flush(Buf *b) {
    if (b->fd >= 0 && b->len > 0) {
        size_t sent = 0;
        while (sent < b->len) {
            long w = sys_write(b->fd, b->data + sent, b->len - sent);
            if (w <= 0) break;
            sent += (size_t)w;
        }
        b->len = 0;
    }
}

static inline void buf_maybe_flush(Buf *b) {
    if (b->fd >= 0 && b->len >= (512ULL << 10)) buf_flush(b);
}

static inline void buf_put(Buf *b, char c) {
    b->data[b->len++] = c;
}
static inline void buf_puts(Buf *b, const char *s, size_t n) {
    fast_copy(b->data + b->len, s, n);
    b->len += n;
}
static inline void buf_put_str(Buf *b, Str s) {
    fast_copy(b->data + b->len, s.p, s.len);
    b->len += s.len;
}
#define buf_lit(b, lit) buf_puts((b), (lit), sizeof(lit) - 1)

static inline void buf_put_u32(Buf *b, uint32_t v) {
    if (v < 10) { b->data[b->len++] = (char)('0' + v); return; }
    if (v < 100) {
        b->data[b->len] = (char)('0' + v / 10);
        b->data[b->len + 1] = (char)('0' + v % 10);
        b->len += 2;
        return;
    }
    char tmp[12];
    int k = 0;
    while (v) { tmp[k++] = (char)('0' + (v % 10)); v /= 10; }
    char *dst = b->data + b->len;
    for (int i = 0; i < k; i++) dst[i] = tmp[k - 1 - i];
    b->len += (size_t)k;
}

// ---- Exact JS UTF-16 code-unit order comparison on UTF-8 strings ----
static inline uint32_t decode_utf8_one(const uint8_t *p, const uint8_t *end, size_t *step) {
    uint8_t c = p[0];
    if (c < 0x80) { *step = 1; return c; }
    if ((c & 0xE0) == 0xC0 && p + 1 < end) {
        *step = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (p[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && p + 2 < end) {
        *step = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && p + 3 < end) {
        *step = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
               ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    }
    *step = 1;
    return c;
}

static int cmp_code_units_slow(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen) {
    const uint8_t *ae = a + alen, *be = b + blen;
    uint16_t a_lo = 0, b_lo = 0;
    while (1) {
        uint16_t au = 0, bu = 0;
        int ha = 0, hb = 0;
        if (a_lo) { au = a_lo; a_lo = 0; ha = 1; }
        else if (a < ae) {
            size_t st; uint32_t cp = decode_utf8_one(a, ae, &st); a += st; ha = 1;
            if (cp > 0xFFFF) { cp -= 0x10000; au = (uint16_t)(0xD800 + (cp >> 10)); a_lo = (uint16_t)(0xDC00 + (cp & 0x3FF)); }
            else au = (uint16_t)cp;
        }
        if (b_lo) { bu = b_lo; b_lo = 0; hb = 1; }
        else if (b < be) {
            size_t st; uint32_t cp = decode_utf8_one(b, be, &st); b += st; hb = 1;
            if (cp > 0xFFFF) { cp -= 0x10000; bu = (uint16_t)(0xD800 + (cp >> 10)); b_lo = (uint16_t)(0xDC00 + (cp & 0x3FF)); }
            else bu = (uint16_t)cp;
        }
        if (!ha || !hb) return ha - hb;
        if (au != bu) return (int)au - (int)bu;
    }
}

static inline int cmp_code_units(Str a, Str b) {
    size_t minlen = a.len < b.len ? a.len : b.len;
    const uint8_t *p = (const uint8_t *)a.p;
    const uint8_t *q = (const uint8_t *)b.p;
    size_t i = 0;
    while (i + 8 <= minlen) {
        uint64_t x, y;
        __builtin_memcpy(&x, p + i, 8);
        __builtin_memcpy(&y, q + i, 8);
        if (x != y) break;
        i += 8;
    }
    while (i < minlen && p[i] == q[i]) i++;
    if (i == minlen) return (a.len > b.len) - (a.len < b.len);
    if (p[i] < 0xEE && q[i] < 0xEE) return (int)p[i] - (int)q[i];
    return cmp_code_units_slow(p, a.len, q, b.len);
}

static inline int str_eq(Str a, Str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.p, b.p, a.len) == 0);
}
static inline int str_eq_lit(Str a, const char *lit, uint32_t l) {
    return a.len == l && memcmp(a.p, lit, l) == 0;
}
#define STR_EQ(s, lit) str_eq_lit((s), (lit), (uint32_t)(sizeof(lit) - 1))

static inline uint64_t hash_bytes(const char *p, size_t len) {
    uint64_t h = 0xcbf29ce484222325ULL ^ (len * 0x9e3779b97f4a7c15ULL);
    const uint8_t *s = (const uint8_t *)p;
    while (len >= 8) {
        uint64_t v;
        __builtin_memcpy(&v, s, 8);
        h ^= v;
        h *= 0x100000001b3ULL;
        h ^= h >> 33;
        s += 8; len -= 8;
    }
    while (len--) {
        h ^= *s++;
        h *= 0x100000001b3ULL;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h ? h : 1;
}

// ---- Runtime SIMD Dispatch ----
static int g_simd_level = 0; // x64: 0=SSE2, 1=SSSE3, 2=AVX2

static void simd_init(void) {
#if defined(__x86_64__)
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    int ssse3 = (ecx >> 9) & 1;
    int osxsave = (ecx >> 27) & 1;
    int avx2 = 0;
    if (osxsave) {
        uint32_t xlo, xhi;
        __asm__ volatile("xgetbv" : "=a"(xlo), "=d"(xhi) : "c"(0));
        if ((xlo & 6) == 6) {
            __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
            avx2 = (ebx >> 5) & 1;
        }
    }
    g_simd_level = avx2 ? 2 : (ssse3 ? 1 : 0);
#else
    g_simd_level = 1;
#endif
}

static const char HEX_LC[16] = "0123456789abcdef";

static inline char *emit_json_ctrl_ptr(char *dst, uint8_t c) {
    switch (c) {
        case '\n': dst[0] = '\\'; dst[1] = 'n'; return dst + 2;
        case '\r': dst[0] = '\\'; dst[1] = 'r'; return dst + 2;
        case '\t': dst[0] = '\\'; dst[1] = 't'; return dst + 2;
        case '\b': dst[0] = '\\'; dst[1] = 'b'; return dst + 2;
        case '\f': dst[0] = '\\'; dst[1] = 'f'; return dst + 2;
        default:
            dst[0] = '\\'; dst[1] = 'u'; dst[2] = '0'; dst[3] = '0';
            dst[4] = HEX_LC[c >> 4]; dst[5] = HEX_LC[c & 15];
            return dst + 6;
    }
}

static inline char *emit_html_json_char_ptr(char *dst, uint8_t c) {
    switch (c) {
        case '&':  fast_copy(dst, "&amp;", 5); return dst + 5;
        case '<':  fast_copy(dst, "&lt;", 4);  return dst + 4;
        case '>':  fast_copy(dst, "&gt;", 4);  return dst + 4;
        case '"':  fast_copy(dst, "&quot;", 6); return dst + 6;
        case '\'': fast_copy(dst, "&#39;", 5); return dst + 5;
        case '\\': dst[0] = '\\'; dst[1] = '\\'; return dst + 2;
        default:   return emit_json_ctrl_ptr(dst, c);
    }
}

static inline void buf_put_json_ctrl(Buf *b, uint8_t c) {
    char *dst = emit_json_ctrl_ptr(b->data + b->len, c);
    b->len = (size_t)(dst - b->data);
}

// ---- Architecture-specific SIMD Scanners & Speculative-Store Mask-Draining Copiers ----
#if defined(__aarch64__)
static inline uint64_t neon_movemask_4bit(uint8x16_t cmp) {
    uint8x8_t res = vshrn_n_u16(vreinterpretq_u16_u8(cmp), 4);
    return vget_lane_u64(vreinterpret_u64_u8(res), 0);
}

// Exact 16-bit mask for NEON (1 bit per lane! Super fast for mask-draining!)
static inline uint16_t neon_movemask_16(uint8x16_t cmp) {
    static const uint8_t BITMASK[16] = { 1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128 };
    uint8x16_t bits = vandq_u8(cmp, vld1q_u8(BITMASK));
    uint8x8_t lo = vget_low_u8(bits);
    uint8x8_t hi = vget_high_u8(bits);
    return (uint16_t)(vaddv_u8(lo) | ((uint16_t)vaddv_u8(hi) << 8));
}

static inline const uint8_t *find_nl(const uint8_t *p, const uint8_t *end) {
    uint8x16_t vnl = vdupq_n_u8('\n');
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        uint64_t m = neon_movemask_4bit(vceqq_u8(v, vnl));
        if (m) return p + (__builtin_ctzll(m) >> 2);
        p += 16;
    }
    while (p < end && *p != '\n') p++;
    return p;
}

static inline const uint8_t *find_json_str_special(const uint8_t *p, const uint8_t *end) {
    uint8x16_t vq = vdupq_n_u8('"');
    uint8x16_t vb = vdupq_n_u8('\\');
    uint8x16_t vc = vdupq_n_u8(0x20);
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        uint8x16_t hit = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, vb)), vcltq_u8(v, vc));
        uint64_t m = neon_movemask_4bit(hit);
        if (m) return p + (__builtin_ctzll(m) >> 2);
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p;
        if (c == '"' || c == '\\' || c < 0x20) return p;
        p++;
    }
    return p;
}

static inline const uint8_t *find_html_json_special(const uint8_t *p, const uint8_t *end) {
    static const uint8_t LO[16] = { 1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1 };
    static const uint8_t HI[16] = { 1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0 };
    uint8x16_t vlo = vld1q_u8(LO);
    uint8x16_t vhi = vld1q_u8(HI);
    uint8x16_t v0f = vdupq_n_u8(0x0F);
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        uint8x16_t mvec = vandq_u8(vqtbl1q_u8(vlo, vandq_u8(v, v0f)), vqtbl1q_u8(vhi, vshrq_n_u8(v, 4)));
        uint64_t m = neon_movemask_4bit(vtstq_u8(mvec, mvec));
        if (m) return p + (__builtin_ctzll(m) >> 2);
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p;
        if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') return p;
        p++;
    }
    return p;
}

// Speculative-store + mask-draining HTML+JSON escape copy (NEON)
static void buf_append_html_json_escaped(Buf *b, const char *s, size_t len) {
    static const uint8_t LO[16] = { 1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1 };
    static const uint8_t HI[16] = { 1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0 };
    uint8x16_t vlo = vld1q_u8(LO);
    uint8x16_t vhi = vld1q_u8(HI);
    uint8x16_t v0f = vdupq_n_u8(0x0F);
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    char *dst = b->data + b->len;
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        vst1q_u8((uint8_t *)dst, v); // speculative store!
        uint8x16_t mvec = vandq_u8(vqtbl1q_u8(vlo, vandq_u8(v, v0f)), vqtbl1q_u8(vhi, vshrq_n_u8(v, 4)));
        uint64_t m = neon_movemask_4bit(vtstq_u8(mvec, mvec));
        if (!m) { p += 16; dst += 16; continue; }
        // Drain every hit in this 16-byte chunk without reloading!
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)(__builtin_ctzll(m) >> 2);
            if (prev > 0 && pos > prev) {
                fast_copy(dst, p + prev, pos - prev);
            }
            dst += (pos - prev);
            dst = emit_html_json_char_ptr(dst, p[pos]);
            prev = pos + 1;
            m &= ~(0xFULL << (pos << 2));
        } while (m);
        if (prev < 16) {
            fast_copy(dst, p + prev, 16 - prev);
            dst += (16 - prev);
        }
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\')
            dst = emit_html_json_char_ptr(dst, c);
        else
            *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}

// Speculative-store + mask-draining JSON escape copy (NEON)
static void buf_append_json_escaped(Buf *b, const char *s, size_t len) {
    uint8x16_t vq = vdupq_n_u8('"');
    uint8x16_t vb = vdupq_n_u8('\\');
    uint8x16_t vc = vdupq_n_u8(0x20);
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    char *dst = b->data + b->len;
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        vst1q_u8((uint8_t *)dst, v);
        uint8x16_t hit = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, vb)), vcltq_u8(v, vc));
        uint64_t m = neon_movemask_4bit(hit);
        if (!m) { p += 16; dst += 16; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)(__builtin_ctzll(m) >> 2);
            if (prev > 0 && pos > prev) fast_copy(dst, p + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = p[pos];
            if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
            else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
            else dst = emit_json_ctrl_ptr(dst, c);
            prev = pos + 1;
            m &= ~(0xFULL << (pos << 2));
        } while (m);
        if (prev < 16) {
            fast_copy(dst, p + prev, 16 - prev);
            dst += (16 - prev);
        }
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
        else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
        else if (c < 0x20) dst = emit_json_ctrl_ptr(dst, c);
        else *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}

// Ultra-fast NEON word counter: 1-instruction ASCII check + exact UTF-8 whitespace guard!
static int64_t count_words_fast_simd(const uint8_t *p, size_t len) {
    uint8x16_t vsp = vdupq_n_u8(' ');
    uint8x16_t v09 = vdupq_n_u8(0x09);
    uint8x16_t v04 = vdupq_n_u8(0x04);
    uint8x16_t v01 = vdupq_n_u8(1);
    uint64_t total = 0;
    uint8_t prev_ws = 0xFF;
    const uint8_t *end = p + len;
    while (p + 16 <= end) {
        uint8x16_t v = vld1q_u8(p);
        if (__builtin_expect(vmaxvq_u8(v) >= 0xC2, 0)) {
            for (int k = 0; k < 16; k++) {
                uint8_t c = p[k];
                if (c == 0xC2 && p + k + 1 < end && p[k + 1] == 0xA0) return -1;
                if (c == 0xE1 && p + k + 2 < end && p[k + 1] == 0x9A && p[k + 2] == 0x80) return -1;
                if (c == 0xE2 && p + k + 2 < end && (p[k + 1] == 0x80 || p[k + 1] == 0x81)) return -1;
                if (c == 0xE3 && p + k + 2 < end && p[k + 1] == 0x80 && p[k + 2] == 0x80) return -1;
                if (c == 0xEF && p + k + 2 < end && p[k + 1] == 0xBB && p[k + 2] == 0xBF) return -1;
            }
        }
        uint8x16_t ws = vorrq_u8(vceqq_u8(v, vsp), vcleq_u8(vsubq_u8(v, v09), v04));
        uint8x16_t prev = vextq_u8(vdupq_n_u8(prev_ws), ws, 15);
        uint8x16_t starts = vbicq_u8(prev, ws);
        total += vaddvq_u8(vandq_u8(starts, v01));
        prev_ws = vgetq_lane_u8(ws, 15);
        p += 16;
    }
    int in_word = (prev_ws == 0);
    while (p < end) {
        uint8_t c = *p++;
        if (__builtin_expect(c >= 0xC2, 0)) {
            if (c == 0xC2 && p < end && p[0] == 0xA0) return -1;
            if (c == 0xE1 && p + 1 < end && p[0] == 0x9A && p[1] == 0x80) return -1;
            if (c == 0xE2 && p + 1 < end && (p[0] == 0x80 || p[0] == 0x81)) return -1;
            if (c == 0xE3 && p + 1 < end && p[0] == 0x80 && p[1] == 0x80) return -1;
            if (c == 0xEF && p + 1 < end && p[0] == 0xBB && p[1] == 0xBF) return -1;
        }
        int ws = (c == ' ' || (c >= 0x09 && c <= 0x0D));
        if (!ws && !in_word) { total++; in_word = 1; }
        else if (ws) { in_word = 0; }
    }
    return (int64_t)total;
}

#elif defined(__x86_64__)
__attribute__((target("avx2")))
static const uint8_t *find_nl_avx2(const uint8_t *p, const uint8_t *end) {
    __m256i vnl = _mm256_set1_epi8('\n');
    while (p + 32 <= end) {
        uint32_t m = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256((const __m256i *)p), vnl));
        if (m) return p + __builtin_ctz(m);
        p += 32;
    }
    while (p < end && *p != '\n') p++;
    return p;
}
static const uint8_t *find_nl_sse2(const uint8_t *p, const uint8_t *end) {
    __m128i vnl = _mm_set1_epi8('\n');
    while (p + 16 <= end) {
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)p), vnl));
        if (m) return p + __builtin_ctz(m);
        p += 16;
    }
    while (p < end && *p != '\n') p++;
    return p;
}
static inline const uint8_t *find_nl(const uint8_t *p, const uint8_t *end) {
    return g_simd_level >= 2 ? find_nl_avx2(p, end) : find_nl_sse2(p, end);
}

__attribute__((target("avx2")))
static const uint8_t *find_json_str_special_avx2(const uint8_t *p, const uint8_t *end) {
    __m256i vq = _mm256_set1_epi8('"'), vb = _mm256_set1_epi8('\\');
    __m256i vc = _mm256_set1_epi8(0x20), vz = _mm256_setzero_si256();
    while (p + 32 <= end) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        __m256i lt20 = _mm256_andnot_si256(_mm256_cmpgt_epi8(vz, v), _mm256_cmpgt_epi8(vc, v));
        __m256i hit = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(v, vq), _mm256_cmpeq_epi8(v, vb)), lt20);
        uint32_t m = (uint32_t)_mm256_movemask_epi8(hit);
        if (m) return p + __builtin_ctz(m);
        p += 32;
    }
    while (p < end) { uint8_t c = *p; if (c == '"' || c == '\\' || c < 0x20) return p; p++; }
    return p;
}
static const uint8_t *find_json_str_special_sse2(const uint8_t *p, const uint8_t *end) {
    __m128i vq = _mm_set1_epi8('"'), vb = _mm_set1_epi8('\\');
    __m128i vc = _mm_set1_epi8(0x20), vz = _mm_setzero_si128();
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        __m128i lt20 = _mm_andnot_si128(_mm_cmplt_epi8(v, vz), _mm_cmplt_epi8(v, vc));
        __m128i hit = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, vb)), lt20);
        uint32_t m = (uint32_t)_mm_movemask_epi8(hit);
        if (m) return p + __builtin_ctz(m);
        p += 16;
    }
    while (p < end) { uint8_t c = *p; if (c == '"' || c == '\\' || c < 0x20) return p; p++; }
    return p;
}
static inline const uint8_t *find_json_str_special(const uint8_t *p, const uint8_t *end) {
    return g_simd_level >= 2 ? find_json_str_special_avx2(p, end) : find_json_str_special_sse2(p, end);
}

__attribute__((target("avx2")))
static const uint8_t *find_html_json_special_avx2(const uint8_t *p, const uint8_t *end) {
    __m256i vlo = _mm256_setr_epi8(1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1,  1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1);
    __m256i vhi = _mm256_setr_epi8(1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0,   1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0);
    __m256i v0f = _mm256_set1_epi8(0x0F), vz = _mm256_setzero_si256();
    while (p + 32 <= end) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(vlo, _mm256_and_si256(v, v0f)),
                                       _mm256_shuffle_epi8(vhi, _mm256_and_si256(_mm256_srli_epi16(v, 4), v0f)));
        uint32_t m = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, vz));
        if (m) return p + __builtin_ctz(m);
        p += 32;
    }
    while (p < end) { uint8_t c = *p; if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') return p; p++; }
    return p;
}
__attribute__((target("ssse3")))
static const uint8_t *find_html_json_special_ssse3(const uint8_t *p, const uint8_t *end) {
    __m128i vlo = _mm_setr_epi8(1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1);
    __m128i vhi = _mm_setr_epi8(1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0);
    __m128i v0f = _mm_set1_epi8(0x0F), vz = _mm_setzero_si128();
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        __m128i hit = _mm_and_si128(_mm_shuffle_epi8(vlo, _mm_and_si128(v, v0f)),
                                    _mm_shuffle_epi8(vhi, _mm_and_si128(_mm_srli_epi16(v, 4), v0f)));
        uint32_t m = (uint32_t)(~_mm_movemask_epi8(_mm_cmpeq_epi8(hit, vz)) & 0xFFFF);
        if (m) return p + __builtin_ctz(m);
        p += 16;
    }
    while (p < end) { uint8_t c = *p; if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') return p; p++; }
    return p;
}
static const uint8_t *find_html_json_special_sse2(const uint8_t *p, const uint8_t *end) {
    __m128i vq = _mm_set1_epi8('"'), va = _mm_set1_epi8('&'), vp = _mm_set1_epi8('\'');
    __m128i vl = _mm_set1_epi8('<'), vg = _mm_set1_epi8('>'), vb = _mm_set1_epi8('\\');
    __m128i vc = _mm_set1_epi8(0x20), vz = _mm_setzero_si128();
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        __m128i h1 = _mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, va));
        __m128i h2 = _mm_or_si128(_mm_cmpeq_epi8(v, vp), _mm_cmpeq_epi8(v, vl));
        __m128i h3 = _mm_or_si128(_mm_cmpeq_epi8(v, vg), _mm_cmpeq_epi8(v, vb));
        __m128i lt20 = _mm_andnot_si128(_mm_cmplt_epi8(v, vz), _mm_cmplt_epi8(v, vc));
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(h1, h2), _mm_or_si128(h3, lt20)));
        if (m) return p + __builtin_ctz(m);
        p += 16;
    }
    while (p < end) { uint8_t c = *p; if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') return p; p++; }
    return p;
}
static inline const uint8_t *find_html_json_special(const uint8_t *p, const uint8_t *end) {
    if (g_simd_level >= 2) return find_html_json_special_avx2(p, end);
    if (g_simd_level == 1) return find_html_json_special_ssse3(p, end);
    return find_html_json_special_sse2(p, end);
}

// Speculative-store + mask-draining HTML+JSON copy (AVX2 / SSSE3 / SSE2)
__attribute__((target("avx2")))
static void buf_append_html_json_avx2(Buf *b, const char *s, size_t len) {
    __m256i vlo = _mm256_setr_epi8(1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1,  1,1,3,1, 1,1,3,3, 1,1,1,1, 13,1,5,1);
    __m256i vhi = _mm256_setr_epi8(1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0,   1,1,2,4, 0,8,0,0, 0,0,0,0, 0,0,0,0);
    __m256i v0f = _mm256_set1_epi8(0x0F), vz = _mm256_setzero_si256();
    const uint8_t *p = (const uint8_t *)s, *end = p + len;
    char *dst = b->data + b->len;
    while (p + 32 <= end) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        _mm256_storeu_si256((__m256i *)dst, v);
        __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(vlo, _mm256_and_si256(v, v0f)),
                                       _mm256_shuffle_epi8(vhi, _mm256_and_si256(_mm256_srli_epi16(v, 4), v0f)));
        uint32_t m = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, vz));
        if (!m) { p += 32; dst += 32; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, p + prev, pos - prev);
            dst += (pos - prev);
            dst = emit_html_json_char_ptr(dst, p[pos]);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 32) { fast_copy(dst, p + prev, 32 - prev); dst += (32 - prev); }
        p += 32;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') dst = emit_html_json_char_ptr(dst, c);
        else *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}
static void buf_append_html_json_sse2(Buf *b, const char *s, size_t len) {
    __m128i vq = _mm_set1_epi8('"'), va = _mm_set1_epi8('&'), vp = _mm_set1_epi8('\'');
    __m128i vl = _mm_set1_epi8('<'), vg = _mm_set1_epi8('>'), vb = _mm_set1_epi8('\\');
    __m128i vc = _mm_set1_epi8(0x20), vz = _mm_setzero_si128();
    const uint8_t *p = (const uint8_t *)s, *end = p + len;
    char *dst = b->data + b->len;
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        _mm_storeu_si128((__m128i *)dst, v);
        __m128i h1 = _mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, va));
        __m128i h2 = _mm_or_si128(_mm_cmpeq_epi8(v, vp), _mm_cmpeq_epi8(v, vl));
        __m128i h3 = _mm_or_si128(_mm_cmpeq_epi8(v, vg), _mm_cmpeq_epi8(v, vb));
        __m128i lt20 = _mm_andnot_si128(_mm_cmplt_epi8(v, vz), _mm_cmplt_epi8(v, vc));
        uint32_t m = (uint32_t)_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(h1, h2), _mm_or_si128(h3, lt20)));
        if (!m) { p += 16; dst += 16; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, p + prev, pos - prev);
            dst += (pos - prev);
            dst = emit_html_json_char_ptr(dst, p[pos]);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 16) { fast_copy(dst, p + prev, 16 - prev); dst += (16 - prev); }
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c < 0x20 || c == '&' || c == '<' || c == '>' || c == '"' || c == '\'' || c == '\\') dst = emit_html_json_char_ptr(dst, c);
        else *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}
static inline void buf_append_html_json_escaped(Buf *b, const char *s, size_t len) {
    if (g_simd_level >= 2) buf_append_html_json_avx2(b, s, len);
    else buf_append_html_json_sse2(b, s, len);
}

__attribute__((target("avx2")))
static void buf_append_json_avx2(Buf *b, const char *s, size_t len) {
    __m256i vq = _mm256_set1_epi8('"'), vb = _mm256_set1_epi8('\\');
    __m256i vc = _mm256_set1_epi8(0x20), vz = _mm256_setzero_si256();
    const uint8_t *p = (const uint8_t *)s, *end = p + len;
    char *dst = b->data + b->len;
    while (p + 32 <= end) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        _mm256_storeu_si256((__m256i *)dst, v);
        __m256i lt20 = _mm256_andnot_si256(_mm256_cmpgt_epi8(vz, v), _mm256_cmpgt_epi8(vc, v));
        __m256i hit = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(v, vq), _mm256_cmpeq_epi8(v, vb)), lt20);
        uint32_t m = (uint32_t)_mm256_movemask_epi8(hit);
        if (!m) { p += 32; dst += 32; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, p + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = p[pos];
            if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
            else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
            else dst = emit_json_ctrl_ptr(dst, c);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 32) { fast_copy(dst, p + prev, 32 - prev); dst += (32 - prev); }
        p += 32;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
        else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
        else if (c < 0x20) dst = emit_json_ctrl_ptr(dst, c);
        else *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}
static void buf_append_json_sse2(Buf *b, const char *s, size_t len) {
    __m128i vq = _mm_set1_epi8('"'), vb = _mm_set1_epi8('\\');
    __m128i vc = _mm_set1_epi8(0x20), vz = _mm_setzero_si128();
    const uint8_t *p = (const uint8_t *)s, *end = p + len;
    char *dst = b->data + b->len;
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        _mm_storeu_si128((__m128i *)dst, v);
        __m128i lt20 = _mm_andnot_si128(_mm_cmplt_epi8(v, vz), _mm_cmplt_epi8(v, vc));
        __m128i hit = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, vb)), lt20);
        uint32_t m = (uint32_t)_mm_movemask_epi8(hit);
        if (!m) { p += 16; dst += 16; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, p + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = p[pos];
            if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
            else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
            else dst = emit_json_ctrl_ptr(dst, c);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 16) { fast_copy(dst, p + prev, 16 - prev); dst += (16 - prev); }
        p += 16;
    }
    while (p < end) {
        uint8_t c = *p++;
        if (c == '"') { dst[0] = '\\'; dst[1] = '"'; dst += 2; }
        else if (c == '\\') { dst[0] = '\\'; dst[1] = '\\'; dst += 2; }
        else if (c < 0x20) dst = emit_json_ctrl_ptr(dst, c);
        else *dst++ = (char)c;
    }
    b->len = (size_t)(dst - b->data);
}
static inline void buf_append_json_escaped(Buf *b, const char *s, size_t len) {
    if (g_simd_level >= 2) buf_append_json_avx2(b, s, len);
    else buf_append_json_sse2(b, s, len);
}

// Ultra-fast x86_64 SIMD word counter (handles non-ws UTF-8 like ·, ﬁnal, Ｗｉｄｅ without aborting!)
static inline int check_uws_chunk(const uint8_t *p, int n, const uint8_t *end) {
    for (int k = 0; k < n; k++) {
        uint8_t c = p[k];
        if (c == 0xC2 && p + k + 1 < end && p[k + 1] == 0xA0) return 1;
        if (c == 0xE1 && p + k + 2 < end && p[k + 1] == 0x9A && p[k + 2] == 0x80) return 1;
        if (c == 0xE2 && p + k + 2 < end && (p[k + 1] == 0x80 || p[k + 1] == 0x81)) return 1;
        if (c == 0xE3 && p + k + 2 < end && p[k + 1] == 0x80 && p[k + 2] == 0x80) return 1;
        if (c == 0xEF && p + k + 2 < end && p[k + 1] == 0xBB && p[k + 2] == 0xBF) return 1;
    }
    return 0;
}

__attribute__((target("avx2")))
static int64_t count_words_avx2(const uint8_t *p, size_t len) {
    __m256i vsp = _mm256_set1_epi8(' '), v08 = _mm256_set1_epi8(0x08), v0e = _mm256_set1_epi8(0x0E);
    uint64_t total = 0;
    uint32_t prev_ws_bit = 1;
    const uint8_t *end = p + len;
    while (p + 32 <= end) {
        __m256i v = _mm256_loadu_si256((const __m256i *)p);
        if (__builtin_expect(_mm256_movemask_epi8(v) != 0, 0)) {
            if (check_uws_chunk(p, 32, end)) return -1;
        }
        __m256i ctrl_ws = _mm256_and_si256(_mm256_cmpgt_epi8(v, v08), _mm256_cmpgt_epi8(v0e, v));
        __m256i ws = _mm256_or_si256(_mm256_cmpeq_epi8(v, vsp), ctrl_ws);
        uint32_t wm = (uint32_t)_mm256_movemask_epi8(ws);
        uint32_t prev_m = (wm << 1) | prev_ws_bit;
        total += (uint32_t)__builtin_popcount(prev_m & ~wm);
        prev_ws_bit = wm >> 31;
        p += 32;
    }
    int in_word = !prev_ws_bit;
    while (p < end) {
        uint8_t c = *p++;
        if (__builtin_expect(c >= 0xC2, 0)) {
            if (check_uws_chunk(p - 1, 1, end)) return -1;
        }
        int ws = (c == ' ' || (c >= 0x09 && c <= 0x0D));
        if (!ws && !in_word) { total++; in_word = 1; }
        else if (ws) { in_word = 0; }
    }
    return (int64_t)total;
}
static int64_t count_words_sse2(const uint8_t *p, size_t len) {
    __m128i vsp = _mm_set1_epi8(' '), v08 = _mm_set1_epi8(0x08), v0e = _mm_set1_epi8(0x0E);
    uint64_t total = 0;
    uint32_t prev_ws_bit = 1;
    const uint8_t *end = p + len;
    while (p + 16 <= end) {
        __m128i v = _mm_loadu_si128((const __m128i *)p);
        if (__builtin_expect(_mm_movemask_epi8(v) != 0, 0)) {
            if (check_uws_chunk(p, 16, end)) return -1;
        }
        __m128i ctrl_ws = _mm_and_si128(_mm_cmpgt_epi8(v, v08), _mm_cmpgt_epi8(v0e, v));
        __m128i ws = _mm_or_si128(_mm_cmpeq_epi8(v, vsp), ctrl_ws);
        uint32_t wm = (uint32_t)_mm_movemask_epi8(ws) & 0xFFFF;
        uint32_t prev_m = ((wm << 1) | prev_ws_bit) & 0xFFFF;
        total += (uint32_t)__builtin_popcount(prev_m & ~wm);
        prev_ws_bit = (wm >> 15) & 1;
        p += 16;
    }
    int in_word = !prev_ws_bit;
    while (p < end) {
        uint8_t c = *p++;
        if (__builtin_expect(c >= 0xC2, 0)) {
            if (check_uws_chunk(p - 1, 1, end)) return -1;
        }
        int ws = (c == ' ' || (c >= 0x09 && c <= 0x0D));
        if (!ws && !in_word) { total++; in_word = 1; }
        else if (ws) { in_word = 0; }
    }
    return (int64_t)total;
}
static inline int64_t count_words_fast_simd(const uint8_t *p, size_t len) {
    return g_simd_level >= 2 ? count_words_avx2(p, len) : count_words_sse2(p, len);
}
#endif

static inline void buf_append_html_raw_escaped(Buf *b, const char *s, size_t len) {
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    while (p < end) {
        const uint8_t *q = find_html_json_special(p, end);
        if (q > p) buf_puts(b, (const char *)p, (size_t)(q - p));
        if (q == end) break;
        uint8_t c = *q++;
        switch (c) {
            case '&': buf_lit(b, "&amp;"); break;
            case '<': buf_lit(b, "&lt;"); break;
            case '>': buf_lit(b, "&gt;"); break;
            case '"': buf_lit(b, "&quot;"); break;
            case '\'': buf_lit(b, "&#39;"); break;
            default: buf_put(b, (char)c); break;
        }
        p = q;
    }
}

static inline Str arena_html_json_escape(Arena *a, Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    const uint8_t *end = p + s.len;
    const uint8_t *q = find_html_json_special(p, end);
    if (q == end) return s;
    char *dst = arena_alloc_str(a, 0);
    Buf b = { dst, 0, 0, -1 };
    if (q > p) buf_puts(&b, (const char *)p, (size_t)(q - p));
    buf_append_html_json_escaped(&b, (const char *)q, (size_t)(end - q));
    a->pos += b.len;
    return (Str){ dst, (uint32_t)b.len };
}

static inline Str arena_json_escape(Arena *a, Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    const uint8_t *end = p + s.len;
    const uint8_t *q = find_json_str_special(p, end);
    if (q == end) return s;
    char *dst = arena_alloc_str(a, 0);
    Buf b = { dst, 0, 0, -1 };
    if (q > p) buf_puts(&b, (const char *)p, (size_t)(q - p));
    buf_append_json_escaped(&b, (const char *)q, (size_t)(end - q));
    a->pos += b.len;
    return (Str){ dst, (uint32_t)b.len };
}
