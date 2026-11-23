#pragma once
#include "sys_simd.h"
#include "text_fm.h"

// ---- Compact Arena-Backed Hash Map / Set (Str -> uint32_t) ----
typedef struct {
    Str key;
    uint64_t hash;
    uint32_t val;
} StrMapSlot;

typedef struct {
    StrMapSlot *slots;
    uint32_t mask;
    uint32_t count;
} StrMap;

static void strmap_init(Arena *a, StrMap *m, uint32_t expected) {
    uint32_t cap = 16;
    while (cap < expected * 2 + 8) cap <<= 1;
    m->slots = (StrMapSlot *)arena_alloc(a, cap * sizeof(StrMapSlot), 8);
    memset(m->slots, 0, cap * sizeof(StrMapSlot));
    m->mask = cap - 1;
    m->count = 0;
}

static inline int strmap_get(const StrMap *m, Str key, uint32_t *out_val) {
    uint64_t h = hash_bytes(key.p, key.len);
    uint32_t i = (uint32_t)h & m->mask;
    for (;;) {
        StrMapSlot *s = &m->slots[i];
        if (s->hash == 0) return 0;
        if (s->hash == h && str_eq(s->key, key)) {
            if (out_val) *out_val = s->val;
            return 1;
        }
        i = (i + 1) & m->mask;
    }
}

static inline int strmap_put(StrMap *m, Str key, uint32_t val) {
    uint64_t h = hash_bytes(key.p, key.len);
    uint32_t i = (uint32_t)h & m->mask;
    for (;;) {
        StrMapSlot *s = &m->slots[i];
        if (s->hash == 0) {
            s->key = key;
            s->hash = h;
            s->val = val;
            m->count++;
            return 1;
        }
        if (s->hash == h && str_eq(s->key, key)) {
            s->val = val;
            return 0;
        }
        i = (i + 1) & m->mask;
    }
}

static Str unique_slug(Arena *a, Str base, StrMap *taken) {
    if (!strmap_get(taken, base, NULL)) {
        strmap_put(taken, base, 1);
        return base;
    }
    char tmp[80];
    fast_copy(tmp, base.p, base.len);
    tmp[base.len] = '-';
    for (uint32_t n = 2;; n++) {
        char num[12];
        int k = 0;
        uint32_t v = n;
        while (v) { num[k++] = (char)('0' + (v % 10)); v /= 10; }
        for (int j = 0; j < k; j++) tmp[base.len + 1 + j] = num[k - 1 - j];
        uint32_t tlen = base.len + 1 + (uint32_t)k;
        Str cand = { tmp, tlen };
        if (!strmap_get(taken, cand, NULL)) {
            char *dst = arena_alloc_str(a, tlen);
            fast_copy(dst, tmp, tlen);
            Str res = { dst, tlen };
            strmap_put(taken, res, 1);
            return res;
        }
    }
}

// Stack-accelerated unique heading ID (avoids 4KB arena zeroing on 99.9% of pages!)
typedef struct {
    Str small[24];
    uint32_t count;
    int use_map;
    StrMap map;
} HeadingIdSet;

static inline int hid_has(const HeadingIdSet *hs, Str s) {
    if (hs->use_map) return strmap_get(&hs->map, s, NULL);
    for (uint32_t i = 0; i < hs->count; i++) if (str_eq(hs->small[i], s)) return 1;
    return 0;
}

static inline void hid_add(Arena *a, HeadingIdSet *hs, Str s) {
    if (!hs->use_map) {
        if (hs->count < 24) { hs->small[hs->count++] = s; return; }
        strmap_init(a, &hs->map, 64);
        for (uint32_t i = 0; i < hs->count; i++) strmap_put(&hs->map, hs->small[i], 1);
        hs->use_map = 1;
    }
    strmap_put(&hs->map, s, 1);
}

static Str unique_heading_id(Arena *a, Str base, HeadingIdSet *hs) {
    if (!hid_has(hs, base)) {
        hid_add(a, hs, base);
        return base;
    }
    char tmp[80];
    fast_copy(tmp, base.p, base.len);
    tmp[base.len] = '-';
    for (uint32_t n = 2;; n++) {
        char num[12];
        int k = 0;
        uint32_t v = n;
        while (v) { num[k++] = (char)('0' + (v % 10)); v /= 10; }
        for (int j = 0; j < k; j++) tmp[base.len + 1 + j] = num[k - 1 - j];
        uint32_t tlen = base.len + 1 + (uint32_t)k;
        Str cand = { tmp, tlen };
        if (!hid_has(hs, cand)) {
            char *dst = arena_alloc_str(a, tlen);
            fast_copy(dst, tmp, tlen);
            Str res = { dst, tlen };
            hid_add(a, hs, res);
            return res;
        }
    }
}

// ---- Link resolution ----
typedef struct {
    uint8_t level;
    Str id;
    Str html;
} TocItem;

typedef struct {
    Arena *arena;
    Str base_url;
    Str src_path;
    Str dir_segs[32];
    int num_dir_segs;
    const StrMap *link_map;    // unified path -> url_prefix (both sources & statics!)
    const Str *link_urls;      // index -> precomputed url prefix
    StrMap *broken_set;
    Str *broken_list;
    uint32_t *broken_count;
    uint32_t broken_cap;
    int direct_json;
} MdEnv;

static const uint8_t MD_ESCAPABLE[128] = {
    ['\\']=1, ['`']=1, ['*']=1, ['_']=1, ['[']=1, [']']=1, ['(']=1,
    [')']=1,  ['#']=1, ['+']=1, ['-']=1, ['.']=1, ['!']=1, ['>']=1
};

// Single-lookup byte class table for inline markdown: 0=plain, 1=html/json escape, 2=md delimiter
static const uint8_t MD_SPEC_LUT[256] = {
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, // 0x00..0x0F
    1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1, // 0x10..0x1F
    0,0,1,0,0,0,1,1, 0,0,2,0,0,0,0,0, // 0x20..0x2F: "=1, &=1, '=1, *=2
    0,0,0,0,0,0,0,0, 0,0,0,0,1,0,1,0, // 0x30..0x3F: <=1, >=1
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, // 0x40..0x4F
    0,0,0,0,0,0,0,0, 0,0,0,2,2,0,0,0, // 0x50..0x5F: [=2, \=2
    2,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, // 0x60..0x6F: `=2
    // 0x70..0xFF all 0
};

static inline int is_md_escapable(uint8_t c) {
    return c < 128 && MD_ESCAPABLE[c];
}

static int resolve_rel_path_pre(const Str *dir_segs, int num_dir, Str rel, char *out_buf, uint32_t *out_len) {
    Str segs[64];
    int nsegs = num_dir;
    for (int k = 0; k < num_dir; k++) segs[k] = dir_segs[k];
    uint32_t st = 0;
    for (uint32_t i = 0; i <= rel.len; i++) {
        if (i == rel.len || rel.p[i] == '/') {
            uint32_t sl = i - st;
            const char *sp = rel.p + st;
            if (sl == 0 || (sl == 1 && sp[0] == '.')) {
            } else if (sl == 2 && sp[0] == '.' && sp[1] == '.') {
                if (nsegs == 0) return 0;
                nsegs--;
            } else {
                if (nsegs < 64) segs[nsegs++] = (Str){ sp, sl };
            }
            st = i + 1;
        }
    }
    uint32_t pos = 0;
    for (int k = 0; k < nsegs; k++) {
        if (k > 0) out_buf[pos++] = '/';
        fast_copy(out_buf + pos, segs[k].p, segs[k].len);
        pos += segs[k].len;
    }
    *out_len = pos;
    return 1;
}

static inline int is_scheme_target(Str t) {
    if (t.len == 0) return 0;
    uint8_t c0 = (uint8_t)t.p[0];
    if (c0 == '/' || c0 == '#') return 1;
    if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z'))) return 0;
    for (uint32_t i = 1; i < t.len; i++) {
        uint8_t c = (uint8_t)t.p[i];
        if (c == ':') return 1;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '.' || c == '-'))
            return 0;
    }
    return 0;
}

static Str resolve_md_link(MdEnv *env, Str target, char *tmp_buf) {
    if (is_scheme_target(target)) return target;
    int32_t hash = -1;
    for (uint32_t i = 0; i < target.len; i++) {
        if (target.p[i] == '#') { hash = (int32_t)i; break; }
    }
    Str rel = (hash < 0) ? target : (Str){ target.p, (uint32_t)hash };
    Str frag = (hash < 0) ? STR_EMPTY : (Str){ target.p + hash, target.len - (uint32_t)hash };
    if (rel.len > 0) {
        char pbuf[1024];
        uint32_t plen = 0;
        if (resolve_rel_path_pre(env->dir_segs, env->num_dir_segs, rel, pbuf, &plen)) {
            Str rpath = { pbuf, plen };
            uint32_t lidx = 0;
            if (strmap_get(env->link_map, rpath, &lidx)) {
                Str url = env->link_urls[lidx];
                if (frag.len == 0) return url;
                fast_copy(tmp_buf, url.p, url.len);
                fast_copy(tmp_buf + url.len, frag.p, frag.len);
                return (Str){ tmp_buf, url.len + frag.len };
            }
        }
    }
    uint32_t blen = 12 + env->src_path.len + 4 + target.len;
    char *bstr = arena_alloc_str(env->arena, blen);
    fast_copy(bstr, "broken-link ", 12);
    fast_copy(bstr + 12, env->src_path.p, env->src_path.len);
    fast_copy(bstr + 12 + env->src_path.len, " -> ", 4);
    fast_copy(bstr + 16 + env->src_path.len, target.p, target.len);
    Str bs = { bstr, blen };
    if (!strmap_get(env->broken_set, bs, NULL)) {
        strmap_put(env->broken_set, bs, 1);
        if (*env->broken_count < env->broken_cap) {
            env->broken_list[(*env->broken_count)++] = bs;
        }
    }
    return target;
}

// ---- Fast inline delimiter searchers ----
static inline int32_t find_unescaped_2star(const char *s, uint32_t len, uint32_t from) {
    for (uint32_t j = from; j + 1 < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == '*') {
            if (s[j + 1] == '*') return (int32_t)j;
        } else if (c == '\\') {
            if (is_md_escapable((uint8_t)s[j + 1])) j++;
        }
    }
    return -1;
}

static inline int32_t find_unescaped_rbracket(const char *s, uint32_t len, uint32_t from) {
    for (uint32_t j = from; j < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == ']') return (int32_t)j;
        if (c == '\\' && j + 1 < len && is_md_escapable((uint8_t)s[j + 1])) j++;
    }
    return -1;
}

static inline int32_t find_single_star(const char *s, uint32_t len, uint32_t from) {
    for (uint32_t j = from; j < len; j++) {
        uint8_t c = (uint8_t)s[j];
        if (c == '*') {
            if (j + 1 < len && s[j + 1] == '*') { j++; continue; }
            return (int32_t)j;
        }
        if (c == '\\' && j + 1 < len && is_md_escapable((uint8_t)s[j + 1])) j++;
    }
    return -1;
}

static inline int has_js_ws(const char *s, uint32_t len) {
    const uint8_t *p = (const uint8_t *)s;
    const uint8_t *end = p + len;
    while (p < end) {
        if (*p < 0x80) {
            if (*p == ' ' || (*p >= 0x09 && *p <= 0x0D)) return 1;
            p++;
        } else {
            size_t st; uint32_t cp = decode_utf8_one(p, end, &st);
            if (is_js_ws_cp(cp)) return 1;
            p += st;
        }
    }
    return 0;
}

static void md_emit_inline(Buf *out, const char *s, uint32_t len, MdEnv *env);

static inline char *md_emit_plain_special(char *dst, uint8_t c, int dj) {
    if (dj && c == '\n') { dst[0] = '\\'; dst[1] = 'n'; return dst + 2; }
    switch (c) {
        case '&':  fast_copy(dst, "&amp;", 5); return dst + 5;
        case '<':  fast_copy(dst, "&lt;", 4);  return dst + 4;
        case '>':  fast_copy(dst, "&gt;", 4);  return dst + 4;
        case '"':  fast_copy(dst, "&quot;", 6); return dst + 6;
        case '\'': fast_copy(dst, "&#39;", 5); return dst + 5;
        default:
            if (dj && c < 0x20) return emit_json_ctrl_ptr(dst, c);
            *dst++ = (char)c;
            return dst;
    }
}

// Fast non-recursive inline emitter for short spans (e.g. *word*, **word**, [two words](url))!
static inline void md_emit_inner_fast(Buf *out, char **dst_io, const char *s, uint32_t len, MdEnv *env) {
    char *dst = *dst_io;
    const int dj = env->direct_json;
    if (len < 32) {
        for (uint32_t k = 0; k < len; k++) {
            uint8_t c = (uint8_t)s[k];
            uint8_t cls = MD_SPEC_LUT[c];
            if (__builtin_expect(cls == 0, 1)) {
                *dst++ = (char)c;
            } else if (cls == 1) {
                dst = md_emit_plain_special(dst, c, dj);
            } else {
                // Nested markdown delimiter inside short span -> delegate remainder to full md_emit_inline!
                out->len = (size_t)(dst - out->data);
                md_emit_inline(out, s + k, len - k, env);
                *dst_io = out->data + out->len;
                return;
            }
        }
        *dst_io = dst;
        return;
    }
    out->len = (size_t)(dst - out->data);
    md_emit_inline(out, s, len, env);
    *dst_io = out->data + out->len;
}

// Handle one Markdown delimiter ('\\', '`', '*', '[') and return new index `i`, updating `dst_io`
static inline uint32_t md_handle_delim(Buf *out, char **dst_io, const char *s, uint32_t len, uint32_t i, MdEnv *env,
                                       uint8_t *no_bt, uint8_t *no_2s, uint8_t *no_1s, uint8_t *no_rb) {
    char *dst = *dst_io;
    const int dj = env->direct_json;
    uint8_t c = (uint8_t)s[i];

    if (c == '\\') {
        if (i + 1 < len && is_md_escapable((uint8_t)s[i + 1])) {
            uint8_t nc = (uint8_t)s[i + 1];
            if (nc == '>') { fast_copy(dst, "&gt;", 4); dst += 4; }
            else if (nc == '\\') { if (dj) { dst[0] = '\\'; dst[1] = '\\'; dst += 2; } else *dst++ = '\\'; }
            else *dst++ = (char)nc;
            *dst_io = dst;
            return i + 2;
        }
        if (dj) { dst[0] = '\\'; dst[1] = '\\'; dst += 2; } else *dst++ = '\\';
        *dst_io = dst;
        return i + 1;
    }
    if (c == '`') {
        if (!*no_bt) {
            uint32_t j = i + 1;
            while (j < len && s[j] != '`') j++;
            if (j < len && j > i + 1) {
                fast_copy(dst, "<code>", 6); dst += 6;
                uint32_t clen = j - (i + 1);
                if (clen < 24 && dj) {
                    const char *cs = s + i + 1;
                    for (uint32_t k = 0; k < clen; k++) {
                        uint8_t cc = (uint8_t)cs[k];
                        if (__builtin_expect(MD_SPEC_LUT[cc] == 0 || cc == '*' || cc == '[' || cc == '`', 1)) {
                            *dst++ = (char)cc;
                        } else {
                            dst = emit_html_json_char_ptr(dst, cc);
                        }
                    }
                } else {
                    out->len = (size_t)(dst - out->data);
                    if (dj) buf_append_html_json_escaped(out, s + i + 1, clen);
                    else buf_append_html_raw_escaped(out, s + i + 1, clen);
                    dst = out->data + out->len;
                }
                fast_copy(dst, "</code>", 7); dst += 7;
                *dst_io = dst;
                return j + 1;
            }
            if (j >= len) *no_bt = 1;
        }
        *dst++ = '`';
        *dst_io = dst;
        return i + 1;
    }
    if (c == '*') {
        if (i + 1 < len && s[i + 1] == '*') {
            if (!*no_2s) {
                int32_t j = find_unescaped_2star(s, len, i + 2);
                if (j > (int32_t)(i + 2)) {
                    fast_copy(dst, "<strong>", 8); dst += 8;
                    md_emit_inner_fast(out, &dst, s + i + 2, (uint32_t)j - (i + 2), env);
                    fast_copy(dst, "</strong>", 9); dst += 9;
                    *dst_io = dst;
                    return (uint32_t)j + 2;
                }
                if (j < 0) *no_2s = 1;
            }
            dst[0] = '*'; dst[1] = '*'; dst += 2;
            *dst_io = dst;
            return i + 2;
        }
        if (!*no_1s) {
            int32_t j = find_single_star(s, len, i + 1);
            if (j > (int32_t)(i + 1)) {
                fast_copy(dst, "<em>", 4); dst += 4;
                md_emit_inner_fast(out, &dst, s + i + 1, (uint32_t)j - (i + 1), env);
                fast_copy(dst, "</em>", 5); dst += 5;
                *dst_io = dst;
                return (uint32_t)j + 1;
            }
            if (j < 0) *no_1s = 1;
        }
        *dst++ = '*';
        *dst_io = dst;
        return i + 1;
    }
    // c == '['
    if (!*no_rb) {
        int32_t k = find_unescaped_rbracket(s, len, i + 1);
        if (k < 0) {
            *no_rb = 1;
        } else if ((uint32_t)(k + 1) < len && s[k + 1] == '(') {
            int32_t m = -1;
            for (uint32_t u = (uint32_t)(k + 2); u < len; u++) {
                if (s[u] == ')') { m = (int32_t)u; break; }
            }
            if (m > k + 2) {
                Str target = { s + k + 2, (uint32_t)(m - (k + 2)) };
                if (!has_js_ws(target.p, target.len)) {
                    if (dj) { fast_copy(dst, "<a href=\\\"", 10); dst += 10; }
                    else    { fast_copy(dst, "<a href=\"", 9); dst += 9; }
                    char lbuf[1024];
                    Str href = resolve_md_link(env, target, lbuf);
                    out->len = (size_t)(dst - out->data);
                    if (dj) buf_append_html_json_escaped(out, href.p, href.len);
                    else buf_append_html_raw_escaped(out, href.p, href.len);
                    dst = out->data + out->len;
                    if (dj) { fast_copy(dst, "\\\">", 3); dst += 3; }
                    else    { fast_copy(dst, "\">", 2); dst += 2; }
                    md_emit_inner_fast(out, &dst, s + i + 1, (uint32_t)k - (i + 1), env);
                    fast_copy(dst, "</a>", 4); dst += 4;
                    *dst_io = dst;
                    return (uint32_t)m + 1;
                }
            }
        }
    }
    *dst++ = '[';
    *dst_io = dst;
    return i + 1;
}

// ---- Speculative-Store + Mask-Draining Inline Renderer (NEON / AVX2 / SSSE3 / SSE2) ----
#if defined(__aarch64__)
static void md_emit_inline(Buf *out, const char *s, uint32_t len, MdEnv *env) {
    const int dj = env->direct_json;
    char *dst = out->data + out->len;
    uint32_t i = 0;
    uint8_t no_bt = 0, no_2s = 0, no_1s = 0, no_rb = 0;

    if (len >= 16) {
        static const uint8_t LO[16] = { 17,1,3,1, 1,1,3,3, 1,1,3,9, 13,1,5,1 };
        static const uint8_t HI[16] = { 1,1,2,4, 0,8,16,0, 0,0,0,0, 0,0,0,0 };
        uint8x16_t vlo = vld1q_u8(LO);
        uint8x16_t vhi = vld1q_u8(HI);
        uint8x16_t v0f = vdupq_n_u8(0x0F);

        while (i + 16 <= len) {
            const uint8_t *blk = (const uint8_t *)(s + i);
            uint8x16_t v = vld1q_u8(blk);
            vst1q_u8((uint8_t *)dst, v);
            uint8x16_t mvec = vandq_u8(vqtbl1q_u8(vlo, vandq_u8(v, v0f)), vqtbl1q_u8(vhi, vshrq_n_u8(v, 4)));
            uint64_t m = neon_movemask_4bit(vtstq_u8(mvec, mvec));
            if (!m) { i += 16; dst += 16; continue; }
            uint32_t prev = 0;
            do {
                uint32_t pos = (uint32_t)(__builtin_ctzll(m) >> 2);
                if (prev > 0 && pos > prev) fast_copy(dst, blk + prev, pos - prev);
                dst += (pos - prev);
                uint8_t c = blk[pos];
                if (c == '*' || c == '[' || c == '`' || c == '\\') {
                    i = md_handle_delim(out, &dst, s, len, i + pos, env, &no_bt, &no_2s, &no_1s, &no_rb);
                    goto next_chunk;
                }
                dst = md_emit_plain_special(dst, c, dj);
                prev = pos + 1;
                m &= ~(0xFULL << (pos << 2));
            } while (m);
            if (prev < 16) {
                fast_copy(dst, blk + prev, 16 - prev);
                dst += (16 - prev);
            }
            i += 16;
            next_chunk:;
        }
    }
    while (i < len) {
        uint8_t c = (uint8_t)s[i];
        uint8_t cls = MD_SPEC_LUT[c];
        if (__builtin_expect(cls == 0, 1)) {
            *dst++ = (char)c;
            i++;
        } else if (cls == 1) {
            dst = md_emit_plain_special(dst, c, dj);
            i++;
        } else {
            i = md_handle_delim(out, &dst, s, len, i, env, &no_bt, &no_2s, &no_1s, &no_rb);
        }
    }
    out->len = (size_t)(dst - out->data);
}
#elif defined(__x86_64__)
__attribute__((target("avx2")))
static void md_emit_inline_avx2(Buf *out, const char *s, uint32_t len, MdEnv *env) {
    __m256i vlo = _mm256_setr_epi8(17,1,3,1, 1,1,3,3, 1,1,3,9, 13,1,5,1,  17,1,3,1, 1,1,3,3, 1,1,3,9, 13,1,5,1);
    __m256i vhi = _mm256_setr_epi8(1,1,2,4, 0,8,16,0, 0,0,0,0, 0,0,0,0,   1,1,2,4, 0,8,16,0, 0,0,0,0, 0,0,0,0);
    __m256i v0f = _mm256_set1_epi8(0x0F), vz = _mm256_setzero_si256();
    const int dj = env->direct_json;
    char *dst = out->data + out->len;
    uint32_t i = 0;
    uint8_t no_bt = 0, no_2s = 0, no_1s = 0, no_rb = 0;

    while (i + 32 <= len) {
        const uint8_t *blk = (const uint8_t *)(s + i);
        __m256i v = _mm256_loadu_si256((const __m256i *)blk);
        _mm256_storeu_si256((__m256i *)dst, v);
        __m256i hit = _mm256_and_si256(_mm256_shuffle_epi8(vlo, _mm256_and_si256(v, v0f)),
                                       _mm256_shuffle_epi8(vhi, _mm256_and_si256(_mm256_srli_epi16(v, 4), v0f)));
        uint32_t m = (uint32_t)~_mm256_movemask_epi8(_mm256_cmpeq_epi8(hit, vz));
        if (!m) { i += 32; dst += 32; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, blk + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = blk[pos];
            if (c == '*' || c == '[' || c == '`' || c == '\\') {
                i = md_handle_delim(out, &dst, s, len, i + pos, env, &no_bt, &no_2s, &no_1s, &no_rb);
                goto next_chunk_avx2;
            }
            dst = md_emit_plain_special(dst, c, dj);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 32) { fast_copy(dst, blk + prev, 32 - prev); dst += (32 - prev); }
        i += 32;
        next_chunk_avx2:;
    }
    while (i < len) {
        uint8_t c = (uint8_t)s[i];
        uint8_t cls = MD_SPEC_LUT[c];
        if (__builtin_expect(cls == 0, 1)) { *dst++ = (char)c; i++; }
        else if (cls == 1) { dst = md_emit_plain_special(dst, c, dj); i++; }
        else { i = md_handle_delim(out, &dst, s, len, i, env, &no_bt, &no_2s, &no_1s, &no_rb); }
    }
    out->len = (size_t)(dst - out->data);
}
__attribute__((target("ssse3")))
static void md_emit_inline_ssse3(Buf *out, const char *s, uint32_t len, MdEnv *env) {
    __m128i vlo = _mm_setr_epi8(17,1,3,1, 1,1,3,3, 1,1,3,9, 13,1,5,1);
    __m128i vhi = _mm_setr_epi8(1,1,2,4, 0,8,16,0, 0,0,0,0, 0,0,0,0);
    __m128i v0f = _mm_set1_epi8(0x0F), vz = _mm_setzero_si128();
    const int dj = env->direct_json;
    char *dst = out->data + out->len;
    uint32_t i = 0;
    uint8_t no_bt = 0, no_2s = 0, no_1s = 0, no_rb = 0;

    while (i + 16 <= len) {
        const uint8_t *blk = (const uint8_t *)(s + i);
        __m128i v = _mm_loadu_si128((const __m128i *)blk);
        _mm_storeu_si128((__m128i *)dst, v);
        __m128i hit = _mm_and_si128(_mm_shuffle_epi8(vlo, _mm_and_si128(v, v0f)),
                                    _mm_shuffle_epi8(vhi, _mm_and_si128(_mm_srli_epi16(v, 4), v0f)));
        uint32_t m = (uint32_t)(~_mm_movemask_epi8(_mm_cmpeq_epi8(hit, vz)) & 0xFFFF);
        if (!m) { i += 16; dst += 16; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, blk + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = blk[pos];
            if (c == '*' || c == '[' || c == '`' || c == '\\') {
                i = md_handle_delim(out, &dst, s, len, i + pos, env, &no_bt, &no_2s, &no_1s, &no_rb);
                goto next_chunk_ssse3;
            }
            dst = md_emit_plain_special(dst, c, dj);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 16) { fast_copy(dst, blk + prev, 16 - prev); dst += (16 - prev); }
        i += 16;
        next_chunk_ssse3:;
    }
    while (i < len) {
        uint8_t c = (uint8_t)s[i];
        uint8_t cls = MD_SPEC_LUT[c];
        if (__builtin_expect(cls == 0, 1)) { *dst++ = (char)c; i++; }
        else if (cls == 1) { dst = md_emit_plain_special(dst, c, dj); i++; }
        else { i = md_handle_delim(out, &dst, s, len, i, env, &no_bt, &no_2s, &no_1s, &no_rb); }
    }
    out->len = (size_t)(dst - out->data);
}
static void md_emit_inline_sse2(Buf *out, const char *s, uint32_t len, MdEnv *env) {
    __m128i vq = _mm_set1_epi8('"'), va = _mm_set1_epi8('&'), vp = _mm_set1_epi8('\'');
    __m128i vl = _mm_set1_epi8('<'), vg = _mm_set1_epi8('>'), vb = _mm_set1_epi8('\\');
    __m128i vs = _mm_set1_epi8('*'), vk = _mm_set1_epi8('['), vt = _mm_set1_epi8('`');
    __m128i vc = _mm_set1_epi8(0x20), vz = _mm_setzero_si128();
    const int dj = env->direct_json;
    char *dst = out->data + out->len;
    uint32_t i = 0;
    uint8_t no_bt = 0, no_2s = 0, no_1s = 0, no_rb = 0;

    while (i + 16 <= len) {
        const uint8_t *blk = (const uint8_t *)(s + i);
        __m128i v = _mm_loadu_si128((const __m128i *)blk);
        _mm_storeu_si128((__m128i *)dst, v);
        __m128i h = _mm_or_si128(_mm_cmpeq_epi8(v, vq), _mm_cmpeq_epi8(v, va));
        h = _mm_or_si128(h, _mm_or_si128(_mm_cmpeq_epi8(v, vp), _mm_cmpeq_epi8(v, vl)));
        h = _mm_or_si128(h, _mm_or_si128(_mm_cmpeq_epi8(v, vg), _mm_cmpeq_epi8(v, vb)));
        h = _mm_or_si128(h, _mm_or_si128(_mm_cmpeq_epi8(v, vs), _mm_cmpeq_epi8(v, vk)));
        h = _mm_or_si128(h, _mm_or_si128(_mm_cmpeq_epi8(v, vt), _mm_andnot_si128(_mm_cmplt_epi8(v, vz), _mm_cmplt_epi8(v, vc))));
        uint32_t m = (uint32_t)_mm_movemask_epi8(h);
        if (!m) { i += 16; dst += 16; continue; }
        uint32_t prev = 0;
        do {
            uint32_t pos = (uint32_t)__builtin_ctz(m);
            if (prev > 0 && pos > prev) fast_copy(dst, blk + prev, pos - prev);
            dst += (pos - prev);
            uint8_t c = blk[pos];
            if (c == '*' || c == '[' || c == '`' || c == '\\') {
                i = md_handle_delim(out, &dst, s, len, i + pos, env, &no_bt, &no_2s, &no_1s, &no_rb);
                goto next_chunk_sse2;
            }
            dst = md_emit_plain_special(dst, c, dj);
            prev = pos + 1;
            m &= m - 1;
        } while (m);
        if (prev < 16) { fast_copy(dst, blk + prev, 16 - prev); dst += (16 - prev); }
        i += 16;
        next_chunk_sse2:;
    }
    while (i < len) {
        uint8_t c = (uint8_t)s[i];
        uint8_t cls = MD_SPEC_LUT[c];
        if (__builtin_expect(cls == 0, 1)) { *dst++ = (char)c; i++; }
        else if (cls == 1) { dst = md_emit_plain_special(dst, c, dj); i++; }
        else { i = md_handle_delim(out, &dst, s, len, i, env, &no_bt, &no_2s, &no_1s, &no_rb); }
    }
    out->len = (size_t)(dst - out->data);
}
static void md_emit_inline(Buf *out, const char *s, uint32_t len, MdEnv *env) {
    if (g_simd_level >= 2) md_emit_inline_avx2(out, s, len, env);
    else if (g_simd_level == 1) md_emit_inline_ssse3(out, s, len, env);
    else md_emit_inline_sse2(out, s, len, env);
}
#endif

// ---- Block Lexer & Renderer ----
static inline int is_blank_line(const uint8_t *p, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (p[i] != ' ' && p[i] != '\t') return 0;
    }
    return 1;
}

static inline Str trim_sp_tab(const uint8_t *p, size_t len) {
    size_t a = 0, b = len;
    while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
    while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
    return (Str){ (const char *)(p + a), (uint32_t)(b - a) };
}

static inline int is_fence_open(const uint8_t *p, size_t len, Str *lang_out) {
    if (len < 3 || p[0] != '`' || p[1] != '`' || p[2] != '`') return 0;
    size_t i = 3;
    while (i < len) {
        uint8_t c = p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-') i++;
        else break;
    }
    size_t lang_end = i;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
    if (i != len) return 0;
    if (lang_out) *lang_out = (Str){ (const char *)(p + 3), (uint32_t)(lang_end - 3) };
    return 1;
}

static inline int is_fence_close(const uint8_t *p, size_t len) {
    if (len < 3 || p[0] != '`' || p[1] != '`' || p[2] != '`') return 0;
    for (size_t i = 3; i < len; i++) if (p[i] != ' ' && p[i] != '\t') return 0;
    return 1;
}

static inline int parse_heading(const uint8_t *p, size_t len, int *depth_out, Str *text_out) {
    if (len < 3 || p[0] != '#') return 0;
    size_t h = 0;
    while (h < len && h < 7 && p[h] == '#') h++;
    if (h < 1 || h > 6 || h >= len || (p[h] != ' ' && p[h] != '\t')) return 0;
    size_t i = h;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) i++;
    size_t e = len;
    while (e > i && (p[e - 1] == ' ' || p[e - 1] == '\t')) e--;
    if (e <= i) return 0;
    if (depth_out) *depth_out = (int)h;
    if (text_out) *text_out = (Str){ (const char *)(p + i), (uint32_t)(e - i) };
    return 1;
}

static inline int is_hr_line(const uint8_t *p, size_t len) {
    if (len < 3) return 0;
    if (!((p[0] == '-' && p[1] == '-' && p[2] == '-') || (p[0] == '*' && p[1] == '*' && p[2] == '*'))) return 0;
    for (size_t i = 3; i < len; i++) if (p[i] != ' ' && p[i] != '\t') return 0;
    return 1;
}

static inline int ends_paragraph(const uint8_t *p, size_t len) {
    if (len == 0) return 1;
    uint8_t c = p[0];
    if (c == ' ' || c == '\t') return is_blank_line(p, len);
    if (c == '`') return is_fence_open(p, len, NULL);
    if (c == '#') return parse_heading(p, len, NULL, NULL);
    if (c == '-' || c == '*') return is_hr_line(p, len);
    return 0;
}

static inline int is_bullet_line(const uint8_t *p, size_t len, Str *rest) {
    if (len >= 2 && (p[0] == '-' || p[0] == '*') && p[1] == ' ') {
        *rest = (Str){ (const char *)(p + 2), (uint32_t)(len - 2) };
        return 1;
    }
    return 0;
}

static inline int is_ordered_line(const uint8_t *p, size_t len, uint32_t *num_out, Str *rest) {
    size_t d = 0;
    uint32_t v = 0;
    while (d < len && d < 9 && p[d] >= '0' && p[d] <= '9') {
        v = v * 10 + (uint32_t)(p[d] - '0');
        d++;
    }
    if (d >= 1 && d + 1 < len && p[d] == '.' && p[d + 1] == ' ') {
        if (num_out) *num_out = v;
        if (rest) *rest = (Str){ (const char *)(p + d + 2), (uint32_t)(len - d - 2) };
        return 1;
    }
    return 0;
}

static inline int is_continued_line(const uint8_t *p, size_t len) {
    if (len < 3 || p[0] != ' ' || p[1] != ' ') return 0;
    size_t i = 2;
    while (i < len && p[i] == ' ') i++;
    if (i >= len) return 0;
    if (p[i] < 0x80) return !(p[i] == ' ' || (p[i] >= 0x09 && p[i] <= 0x0D));
    size_t st; uint32_t cp = decode_utf8_one(p + i, p + len, &st);
    return !is_js_ws_cp(cp);
}

typedef struct {
    Str html;
    Str excerpt;
    TocItem *toc;
    uint32_t num_toc;
} MdResult;

static void render_markdown(Str body, MdEnv *env, Buf *scratch_html, Buf *scratch_tmp, MdResult *res) {
    Arena *a = env->arena;
    const int dj = env->direct_json;
    HeadingIdSet heading_ids;
    heading_ids.count = 0;
    heading_ids.use_map = 0;

    TocItem toc_stack[128];
    TocItem *toc_arr = toc_stack;
    uint32_t toc_cap = 128, toc_cnt = 0;

    size_t html_start = scratch_html->len;
    int have_excerpt = 0;
    res->excerpt = STR_EMPTY;

    const uint8_t *p = (const uint8_t *)body.p;
    const uint8_t *end = p + body.len;
    const uint8_t *cur = p;

    while (cur <= end) {
        const uint8_t *nl = find_nl(cur, end);
        size_t llen = (size_t)(nl - cur);
        const uint8_t *next_cur = (nl < end) ? (nl + 1) : (end + 1);

        if (llen == 0 || ((cur[0] == ' ' || cur[0] == '\t') && is_blank_line(cur, llen))) {
            cur = next_cur;
            continue;
        }
        uint8_t c0 = cur[0];
        // 1. Fence
        Str lang;
        if (c0 == '`' && is_fence_open(cur, llen, &lang)) {
            if (lang.len == 0) {
                buf_lit(scratch_html, "<pre><code>");
            } else {
                if (dj) buf_lit(scratch_html, "<pre><code class=\\\"language-");
                else buf_lit(scratch_html, "<pre><code class=\"language-");
                buf_put_str(scratch_html, lang);
                if (dj) buf_lit(scratch_html, "\\\">");
                else buf_lit(scratch_html, "\">");
            }
            cur = next_cur;
            // Fast bulk fence body: find closing fence line and HTML+JSON escape the entire block in ONE SIMD pass!
            const uint8_t *body_start = cur;
            const uint8_t *body_end = cur;
            int closed_at_eof_no_nl = 0;
            while (cur <= end) {
                const uint8_t *fl = find_nl(cur, end);
                size_t flen = (size_t)(fl - cur);
                const uint8_t *fnext = (fl < end) ? (fl + 1) : (end + 1);
                if (is_fence_close(cur, flen)) {
                    body_end = cur;
                    cur = fnext;
                    goto emit_fence;
                }
                if (fl == end) {
                    // Unclosed fence at EOF: last line also gets a trailing '\n'
                    body_end = end;
                    closed_at_eof_no_nl = 1;
                    cur = fnext;
                    goto emit_fence;
                }
                cur = fnext;
            }
            emit_fence:
            if (body_end > body_start) {
                if (dj) buf_append_html_json_escaped(scratch_html, (const char *)body_start, (size_t)(body_end - body_start));
                else buf_append_html_raw_escaped(scratch_html, (const char *)body_start, (size_t)(body_end - body_start));
            }
            if (closed_at_eof_no_nl) {
                if (dj) buf_lit(scratch_html, "\\n"); else buf_put(scratch_html, '\n');
            }
            if (dj) buf_lit(scratch_html, "</code></pre>\\n");
            else buf_lit(scratch_html, "</code></pre>\n");
            continue;
        }
        // 2. Heading
        int depth;
        Str htext;
        if (c0 == '#' && parse_heading(cur, llen, &depth, &htext)) {
            Str hid = unique_heading_id(a, slugify(a, htext), &heading_ids);
            if (dj) {
                buf_lit(scratch_html, "<h"); buf_put(scratch_html, (char)('0' + depth));
                buf_lit(scratch_html, " id=\\\""); buf_put_str(scratch_html, hid); buf_lit(scratch_html, "\\\">");
            } else {
                buf_lit(scratch_html, "<h"); buf_put(scratch_html, (char)('0' + depth));
                buf_lit(scratch_html, " id=\""); buf_put_str(scratch_html, hid); buf_lit(scratch_html, "\">");
            }
            size_t h_in_start = scratch_html->len;
            md_emit_inline(scratch_html, htext.p, htext.len, env);
            size_t h_in_len = scratch_html->len - h_in_start;
            if (depth == 2 || depth == 3) {
                if (toc_cnt == toc_cap) {
                    uint32_t ncap = toc_cap * 2;
                    TocItem *nt = (TocItem *)arena_alloc(a, ncap * sizeof(TocItem), 8);
                    memcpy(nt, toc_arr, toc_cnt * sizeof(TocItem));
                    toc_arr = nt; toc_cap = ncap;
                }
                toc_arr[toc_cnt++] = (TocItem){ (uint8_t)depth, hid, { scratch_html->data + h_in_start, (uint32_t)h_in_len } };
            }
            buf_lit(scratch_html, "</h"); buf_put(scratch_html, (char)('0' + depth));
            if (dj) buf_lit(scratch_html, ">\\n"); else buf_lit(scratch_html, ">\n");
            cur = next_cur;
            continue;
        }
        // 3. Thematic break
        if ((c0 == '-' || c0 == '*') && is_hr_line(cur, llen)) {
            if (dj) buf_lit(scratch_html, "<hr>\\n"); else buf_lit(scratch_html, "<hr>\n");
            cur = next_cur;
            continue;
        }
        // 4. Blockquote
        if (c0 == '>') {
            scratch_tmp->len = 0;
            int qcount = 0;
            while (cur <= end) {
                const uint8_t *ql = find_nl(cur, end);
                size_t qlen = (size_t)(ql - cur);
                if (qlen == 0 || cur[0] != '>') break;
                size_t skip = (qlen >= 2 && cur[1] == ' ') ? 2 : 1;
                Str trimmed = trim_sp_tab(cur + skip, qlen - skip);
                if (trimmed.len > 0) {
                    if (qcount > 0) buf_put(scratch_tmp, '\n');
                    buf_put_str(scratch_tmp, trimmed);
                    qcount++;
                }
                cur = (ql < end) ? (ql + 1) : (end + 1);
            }
            if (qcount == 0) {
                if (dj) buf_lit(scratch_html, "<blockquote>\\n</blockquote>\\n");
                else buf_lit(scratch_html, "<blockquote>\n</blockquote>\n");
            } else {
                if (dj) buf_lit(scratch_html, "<blockquote>\\n<p>");
                else buf_lit(scratch_html, "<blockquote>\n<p>");
                md_emit_inline(scratch_html, scratch_tmp->data, (uint32_t)scratch_tmp->len, env);
                if (dj) buf_lit(scratch_html, "</p>\\n</blockquote>\\n");
                else buf_lit(scratch_html, "</p>\n</blockquote>\n");
            }
            continue;
        }
        // 5. List (bullet or ordered) — zero-copy single-line items!
        Str lrest;
        uint32_t ostart = 1;
        int is_bullet = (c0 == '-' || c0 == '*') && is_bullet_line(cur, llen, &lrest);
        int is_ordered = !is_bullet && (c0 >= '0' && c0 <= '9') && is_ordered_line(cur, llen, &ostart, &lrest);
        if (is_bullet || is_ordered) {
            if (is_ordered) {
                if (ostart != 1) {
                    if (dj) { buf_lit(scratch_html, "<ol start=\\\""); buf_put_u32(scratch_html, ostart); buf_lit(scratch_html, "\\\">\\n"); }
                    else { buf_lit(scratch_html, "<ol start=\""); buf_put_u32(scratch_html, ostart); buf_lit(scratch_html, "\">\n"); }
                } else {
                    if (dj) buf_lit(scratch_html, "<ol>\\n"); else buf_lit(scratch_html, "<ol>\n");
                }
            } else {
                if (dj) buf_lit(scratch_html, "<ul>\\n"); else buf_lit(scratch_html, "<ul>\n");
            }
            Str cur_item = STR_EMPTY;
            int item_in_tmp = 0, have_item = 0;
            while (cur <= end) {
                const uint8_t *ll = find_nl(cur, end);
                size_t len_l = (size_t)(ll - cur);
                Str irest;
                int matched = is_ordered ? is_ordered_line(cur, len_l, NULL, &irest) : is_bullet_line(cur, len_l, &irest);
                if (matched) {
                    if (have_item) {
                        buf_lit(scratch_html, "<li>");
                        if (item_in_tmp) md_emit_inline(scratch_html, scratch_tmp->data, (uint32_t)scratch_tmp->len, env);
                        else md_emit_inline(scratch_html, cur_item.p, cur_item.len, env);
                        if (dj) buf_lit(scratch_html, "</li>\\n"); else buf_lit(scratch_html, "</li>\n");
                    }
                    cur_item = trim_sp_tab((const uint8_t *)irest.p, irest.len);
                    item_in_tmp = 0;
                    have_item = 1;
                    cur = (ll < end) ? (ll + 1) : (end + 1);
                } else if (have_item && is_continued_line(cur, len_l)) {
                    if (!item_in_tmp) {
                        scratch_tmp->len = 0;
                        buf_put_str(scratch_tmp, cur_item);
                        item_in_tmp = 1;
                    }
                    buf_put(scratch_tmp, '\n');
                    Str tr = trim_sp_tab(cur, len_l);
                    buf_put_str(scratch_tmp, tr);
                    cur = (ll < end) ? (ll + 1) : (end + 1);
                } else {
                    break;
                }
            }
            if (have_item) {
                buf_lit(scratch_html, "<li>");
                if (item_in_tmp) md_emit_inline(scratch_html, scratch_tmp->data, (uint32_t)scratch_tmp->len, env);
                else md_emit_inline(scratch_html, cur_item.p, cur_item.len, env);
                if (dj) buf_lit(scratch_html, "</li>\\n"); else buf_lit(scratch_html, "</li>\n");
            }
            if (is_ordered) {
                if (dj) buf_lit(scratch_html, "</ol>\\n"); else buf_lit(scratch_html, "</ol>\n");
            } else {
                if (dj) buf_lit(scratch_html, "</ul>\\n"); else buf_lit(scratch_html, "</ul>\n");
            }
            continue;
        }
        // 6. Paragraph — ZERO-COPY fast path when no line needs trimming (90%+ of all paragraphs!)
        const uint8_t *para_start = cur;
        const uint8_t *para_end = nl;
        int dirty = (cur[0] == ' ' || cur[0] == '\t' || cur[llen - 1] == ' ' || cur[llen - 1] == '\t');
        if (!dirty) {
            cur = next_cur;
            while (cur <= end) {
                const uint8_t *pl = find_nl(cur, end);
                size_t plen = (size_t)(pl - cur);
                if (ends_paragraph(cur, plen)) break;
                if (cur[0] == ' ' || cur[0] == '\t' || cur[plen - 1] == ' ' || cur[plen - 1] == '\t') {
                    // Spill clean prefix to scratch_tmp and continue in dirty mode
                    scratch_tmp->len = 0;
                    buf_puts(scratch_tmp, (const char *)para_start, (size_t)(para_end - para_start));
                    buf_put(scratch_tmp, '\n');
                    buf_put_str(scratch_tmp, trim_sp_tab(cur, plen));
                    dirty = 1;
                    cur = (pl < end) ? (pl + 1) : (end + 1);
                    break;
                }
                para_end = pl;
                cur = (pl < end) ? (pl + 1) : (end + 1);
            }
        } else {
            scratch_tmp->len = 0;
            buf_put_str(scratch_tmp, trim_sp_tab(cur, llen));
            cur = next_cur;
        }
        if (dirty) {
            while (cur <= end) {
                const uint8_t *pl = find_nl(cur, end);
                size_t plen = (size_t)(pl - cur);
                if (ends_paragraph(cur, plen)) break;
                buf_put(scratch_tmp, '\n');
                buf_put_str(scratch_tmp, trim_sp_tab(cur, plen));
                cur = (pl < end) ? (pl + 1) : (end + 1);
            }
        }
        buf_lit(scratch_html, "<p>");
        size_t p_in_start = scratch_html->len;
        if (!dirty) {
            md_emit_inline(scratch_html, (const char *)para_start, (uint32_t)(para_end - para_start), env);
        } else {
            md_emit_inline(scratch_html, scratch_tmp->data, (uint32_t)scratch_tmp->len, env);
        }
        size_t p_in_len = scratch_html->len - p_in_start;
        if (!have_excerpt) {
            have_excerpt = 1;
            res->excerpt = (Str){ scratch_html->data + p_in_start, (uint32_t)p_in_len };
        }
        if (dj) buf_lit(scratch_html, "</p>\\n"); else buf_lit(scratch_html, "</p>\n");
    }

    res->html = (Str){ scratch_html->data + html_start, (uint32_t)(scratch_html->len - html_start) };
    if (toc_cnt > 0 && toc_arr == toc_stack) {
        TocItem *copy = (TocItem *)arena_alloc(a, toc_cnt * sizeof(TocItem), 8);
        memcpy(copy, toc_stack, toc_cnt * sizeof(TocItem));
        res->toc = copy;
    } else {
        res->toc = toc_cnt ? toc_arr : NULL;
    }
    res->num_toc = toc_cnt;
}
