#pragma once
#include "sys_simd.h"

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
    Arena *arena;
} JsonParser;

static inline void jp_skip_ws(JsonParser *jp) {
    const uint8_t *p = jp->p, *end = jp->end;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    jp->p = p;
}

static inline int hex_val(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static inline uint32_t parse_hex4(const uint8_t *p) {
    return ((uint32_t)hex_val(p[0]) << 12) | ((uint32_t)hex_val(p[1]) << 8) |
           ((uint32_t)hex_val(p[2]) << 4) | (uint32_t)hex_val(p[3]);
}

static inline size_t encode_utf8(char *dst, uint32_t cp) {
    if (cp < 0x80) { dst[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

// Ultra-fast speculative-store SIMD JSON string parser with fused \r\n -> \n normalization!
static Str jp_parse_string(JsonParser *jp, int normalize_cr) {
    jp_skip_ws(jp);
    const uint8_t *p = jp->p;
    const uint8_t *end = jp->end;
    if (p >= end || *p != '"') return STR_EMPTY;
    p++;
    const uint8_t *start = p;
    const uint8_t *q = find_json_str_special(p, end);
    if (q < end && *q == '"') {
        // Zero-copy fast path!
        jp->p = q + 1;
        return (Str){ (const char *)start, (uint32_t)(q - start) };
    }
    char *out_base = arena_alloc_str(jp->arena, 0);
    char *dst = out_base;
    if (q > start) {
        fast_copy(dst, start, (size_t)(q - start));
        dst += (size_t)(q - start);
    }
    p = q;
    int prev_cr = 0;

#if defined(__aarch64__)
    uint8x16_t vq = vdupq_n_u8('"');
    uint8x16_t vb = vdupq_n_u8('\\');
    uint8x16_t vc = vdupq_n_u8(0x20);
#endif

    while (p < end) {
        uint8_t c = *p++;
        if (c == '"') break;
        if (c == '\\' && p < end) {
            uint8_t e = *p++;
            if (e == 'n') {
                if (normalize_cr && prev_cr) prev_cr = 0;
                else { *dst++ = '\n'; prev_cr = 0; }
            } else if (e == '"') {
                *dst++ = '"'; prev_cr = 0;
            } else if (e == 'r') {
                if (normalize_cr) { *dst++ = '\n'; prev_cr = 1; }
                else *dst++ = '\r';
            } else if (e == '\\') {
                *dst++ = '\\'; prev_cr = 0;
            } else if (e == 't') {
                *dst++ = '\t'; prev_cr = 0;
            } else if (e == '/') {
                *dst++ = '/'; prev_cr = 0;
            } else if (e == 'b') {
                *dst++ = '\b'; prev_cr = 0;
            } else if (e == 'f') {
                *dst++ = '\f'; prev_cr = 0;
            } else if (e == 'u' && p + 4 <= end) {
                uint32_t cp = parse_hex4(p);
                p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end && p[0] == '\\' && p[1] == 'u') {
                    uint32_t lo = parse_hex4(p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
                        p += 6;
                    }
                }
                if (normalize_cr && cp == '\r') { *dst++ = '\n'; prev_cr = 1; }
                else if (normalize_cr && cp == '\n' && prev_cr) prev_cr = 0;
                else { dst += encode_utf8(dst, cp); prev_cr = 0; }
            } else {
                *dst++ = (char)e; prev_cr = 0;
            }
        } else {
            if (normalize_cr && c == '\r') { *dst++ = '\n'; prev_cr = 1; }
            else if (normalize_cr && c == '\n' && prev_cr) prev_cr = 0;
            else { *dst++ = (char)c; prev_cr = 0; }
        }
        // Speculative-store SIMD bulk copy up to next special!
#if defined(__aarch64__)
        while (p + 16 <= end) {
            uint8x16_t v = vld1q_u8(p);
            vst1q_u8((uint8_t *)dst, v);
            uint8x16_t hit = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, vb)), vcltq_u8(v, vc));
            uint64_t m = neon_movemask_4bit(hit);
            if (!m) { p += 16; dst += 16; prev_cr = 0; continue; }
            uint32_t pos = (uint32_t)(__builtin_ctzll(m) >> 2);
            if (pos > 0) { p += pos; dst += pos; prev_cr = 0; }
            break;
        }
        if (p + 16 > end) {
            const uint8_t *nxt = find_json_str_special(p, end);
            if (nxt > p) { fast_copy(dst, p, (size_t)(nxt - p)); dst += (size_t)(nxt - p); prev_cr = 0; p = nxt; }
        }
#elif defined(__x86_64__)
        {
            __m128i xq = _mm_set1_epi8('"'), xb = _mm_set1_epi8('\\');
            __m128i xc = _mm_set1_epi8(0x20), xz = _mm_setzero_si128();
            while (p + 16 <= end) {
                __m128i v = _mm_loadu_si128((const __m128i *)p);
                _mm_storeu_si128((__m128i *)dst, v);
                __m128i lt20 = _mm_andnot_si128(_mm_cmplt_epi8(v, xz), _mm_cmplt_epi8(v, xc));
                __m128i hit = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(v, xq), _mm_cmpeq_epi8(v, xb)), lt20);
                uint32_t m = (uint32_t)_mm_movemask_epi8(hit);
                if (!m) { p += 16; dst += 16; prev_cr = 0; continue; }
                uint32_t pos = (uint32_t)__builtin_ctz(m);
                if (pos > 0) { p += pos; dst += pos; prev_cr = 0; }
                break;
            }
            if (p + 16 > end) {
                const uint8_t *nxt = find_json_str_special(p, end);
                if (nxt > p) { fast_copy(dst, p, (size_t)(nxt - p)); dst += (size_t)(nxt - p); prev_cr = 0; p = nxt; }
            }
        }
#else
        const uint8_t *nxt = find_json_str_special(p, end);
        if (nxt > p) {
            fast_copy(dst, p, (size_t)(nxt - p));
            dst += (size_t)(nxt - p);
            prev_cr = 0;
            p = nxt;
        }
#endif
    }
    size_t len = (size_t)(dst - out_base);
    jp->arena->pos += len;
    jp->p = p;
    return (Str){ out_base, (uint32_t)len };
}

static int64_t jp_parse_int(JsonParser *jp) {
    jp_skip_ws(jp);
    const uint8_t *p = jp->p, *end = jp->end;
    int neg = 0;
    if (p < end && *p == '-') { neg = 1; p++; }
    int64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + (*p++ - '0');
    }
    while (p < end && (*p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-' || (*p >= '0' && *p <= '9'))) p++;
    jp->p = p;
    return neg ? -v : v;
}

static void jp_skip_value(JsonParser *jp) {
    jp_skip_ws(jp);
    if (jp->p >= jp->end) return;
    uint8_t c = *jp->p;
    if (c == '"') {
        const uint8_t *p = jp->p + 1, *end = jp->end;
        while (p < end) {
            const uint8_t *q = find_json_str_special(p, end);
            if (q >= end) { p = end; break; }
            if (*q == '"') { p = q + 1; break; }
            if (*q == '\\') { p = q + 2; continue; }
            p = q + 1;
        }
        jp->p = p;
        return;
    }
    if (c == '{' || c == '[') {
        uint8_t close = (c == '{') ? '}' : ']';
        jp->p++;
        jp_skip_ws(jp);
        if (jp->p < jp->end && *jp->p == close) { jp->p++; return; }
        while (jp->p < jp->end) {
            if (c == '{') {
                jp_skip_value(jp);
                jp_skip_ws(jp);
                if (jp->p < jp->end && *jp->p == ':') jp->p++;
            }
            jp_skip_value(jp);
            jp_skip_ws(jp);
            if (jp->p < jp->end && *jp->p == ',') { jp->p++; continue; }
            if (jp->p < jp->end && *jp->p == close) { jp->p++; break; }
            break;
        }
        return;
    }
    while (jp->p < jp->end) {
        uint8_t ch = *jp->p;
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') break;
        jp->p++;
    }
}

typedef struct {
    Str name;
    Str src;
} RawTemplate;

typedef struct {
    Str path;
    Str content;
    int normalized;
} RawFile;

typedef struct {
    Str site_title;
    Str site_base_url;
    int32_t per_page;
    int32_t recent;
    RawTemplate *templates;
    uint32_t num_templates;
    uint32_t cap_templates;
    RawFile *files;
    uint32_t num_files;
    uint32_t cap_files;
} CaseInput;

static int parse_case_input(JsonParser *jp, CaseInput *ci) {
    memset(ci, 0, sizeof(*ci));
    ci->cap_templates = 64;
    ci->templates = (RawTemplate *)arena_alloc(jp->arena, ci->cap_templates * sizeof(RawTemplate), 8);
    ci->cap_files = 1024;
    ci->files = (RawFile *)arena_alloc(jp->arena, ci->cap_files * sizeof(RawFile), 8);

    jp_skip_ws(jp);
    if (jp->p >= jp->end || *jp->p != '{') return 0;
    jp->p++;
    while (jp->p < jp->end) {
        jp_skip_ws(jp);
        if (*jp->p == '}') { jp->p++; break; }
        Str key = jp_parse_string(jp, 0);
        jp_skip_ws(jp);
        if (jp->p < jp->end && *jp->p == ':') jp->p++;
        if (STR_EQ(key, "site")) {
            jp_skip_ws(jp);
            if (jp->p < jp->end && *jp->p == '{') {
                jp->p++;
                while (jp->p < jp->end) {
                    jp_skip_ws(jp);
                    if (*jp->p == '}') { jp->p++; break; }
                    Str sk = jp_parse_string(jp, 0);
                    jp_skip_ws(jp);
                    if (jp->p < jp->end && *jp->p == ':') jp->p++;
                    if (STR_EQ(sk, "title")) ci->site_title = jp_parse_string(jp, 0);
                    else if (STR_EQ(sk, "base_url")) ci->site_base_url = jp_parse_string(jp, 0);
                    else if (STR_EQ(sk, "per_page")) ci->per_page = (int32_t)jp_parse_int(jp);
                    else if (STR_EQ(sk, "recent")) ci->recent = (int32_t)jp_parse_int(jp);
                    else jp_skip_value(jp);
                    jp_skip_ws(jp);
                    if (jp->p < jp->end && *jp->p == ',') jp->p++;
                }
            }
        } else if (STR_EQ(key, "templates")) {
            jp_skip_ws(jp);
            if (jp->p < jp->end && *jp->p == '{') {
                jp->p++;
                while (jp->p < jp->end) {
                    jp_skip_ws(jp);
                    if (*jp->p == '}') { jp->p++; break; }
                    Str tname = jp_parse_string(jp, 0);
                    jp_skip_ws(jp);
                    if (jp->p < jp->end && *jp->p == ':') jp->p++;
                    Str tsrc = jp_parse_string(jp, 0);
                    if (ci->num_templates == ci->cap_templates) {
                        uint32_t ncap = ci->cap_templates * 2;
                        RawTemplate *nt = (RawTemplate *)arena_alloc(jp->arena, ncap * sizeof(RawTemplate), 8);
                        memcpy(nt, ci->templates, ci->num_templates * sizeof(RawTemplate));
                        ci->templates = nt; ci->cap_templates = ncap;
                    }
                    ci->templates[ci->num_templates++] = (RawTemplate){ tname, tsrc };
                    jp_skip_ws(jp);
                    if (jp->p < jp->end && *jp->p == ',') jp->p++;
                }
            }
        } else if (STR_EQ(key, "files")) {
            jp_skip_ws(jp);
            if (jp->p < jp->end && *jp->p == '[') {
                jp->p++;
                while (jp->p < jp->end) {
                    jp_skip_ws(jp);
                    if (*jp->p == ']') { jp->p++; break; }
                    if (*jp->p == '{') {
                        jp->p++;
                        Str fpath = STR_EMPTY, fcontent = STR_EMPTY;
                        int have_path = 0, norm = 0;
                        while (jp->p < jp->end) {
                            jp_skip_ws(jp);
                            if (*jp->p == '}') { jp->p++; break; }
                            Str fk = jp_parse_string(jp, 0);
                            jp_skip_ws(jp);
                            if (jp->p < jp->end && *jp->p == ':') jp->p++;
                            if (STR_EQ(fk, "path")) {
                                fpath = jp_parse_string(jp, 0);
                                have_path = 1;
                            } else if (STR_EQ(fk, "content")) {
                                int is_md = have_path && fpath.len >= 3 &&
                                    fpath.p[fpath.len-3] == '.' && fpath.p[fpath.len-2] == 'm' && fpath.p[fpath.len-1] == 'd';
                                fcontent = jp_parse_string(jp, is_md);
                                norm = is_md;
                            } else jp_skip_value(jp);
                            jp_skip_ws(jp);
                            if (jp->p < jp->end && *jp->p == ',') jp->p++;
                        }
                        if (ci->num_files == ci->cap_files) {
                            uint32_t ncap = ci->cap_files * 2;
                            RawFile *nf = (RawFile *)arena_alloc(jp->arena, ncap * sizeof(RawFile), 8);
                            memcpy(nf, ci->files, ci->num_files * sizeof(RawFile));
                            ci->files = nf; ci->cap_files = ncap;
                        }
                        ci->files[ci->num_files++] = (RawFile){ fpath, fcontent, norm };
                    }
                    jp_skip_ws(jp);
                    if (jp->p < jp->end && *jp->p == ',') jp->p++;
                }
            }
        } else {
            jp_skip_value(jp);
        }
        jp_skip_ws(jp);
        if (jp->p < jp->end && *jp->p == ',') jp->p++;
    }
    return 1;
}
