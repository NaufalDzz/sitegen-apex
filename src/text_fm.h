#pragma once
#include "sys_simd.h"
#include "unicode_slug.h"

// ---- Exact Unicode NFKD + \p{M} drop slugify ----
static inline int is_mark_cp(uint32_t cp) {
    int lo = 0, hi = SLUG_MARK_RANGES_LEN - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (cp < SLUG_MARK_LO[mid]) hi = mid - 1;
        else if (cp > SLUG_MARK_HI[mid]) lo = mid + 1;
        else return 1;
    }
    return 0;
}

static inline int lookup_slug_map(uint32_t cp) {
    int lo = 0, hi = SLUG_MAP_LEN - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t v = SLUG_MAP_CP[mid];
        if (cp < v) hi = mid - 1;
        else if (cp > v) lo = mid + 1;
        else return mid;
    }
    return -1;
}

static const uint8_t SLUG_ASCII[128] = {
    ['0']='0',['1']='1',['2']='2',['3']='3',['4']='4',['5']='5',['6']='6',['7']='7',['8']='8',['9']='9',
    ['A']='a',['B']='b',['C']='c',['D']='d',['E']='e',['F']='f',['G']='g',['H']='h',['I']='i',['J']='j',
    ['K']='k',['L']='l',['M']='m',['N']='n',['O']='o',['P']='p',['Q']='q',['R']='r',['S']='s',['T']='t',
    ['U']='u',['V']='v',['W']='w',['X']='x',['Y']='y',['Z']='z',
    ['a']='a',['b']='b',['c']='c',['d']='d',['e']='e',['f']='f',['g']='g',['h']='h',['i']='i',['j']='j',
    ['k']='k',['l']='l',['m']='m',['n']='n',['o']='o',['p']='p',['q']='q',['r']='r',['s']='s',['t']='t',
    ['u']='u',['v']='v',['w']='w',['x']='x',['y']='y',['z']='z',
};

static Str slugify(Arena *a, Str text) {
    char tmp[80];
    uint32_t olen = 0;
    int gap = 0;
    const uint8_t *p = (const uint8_t *)text.p;
    const uint8_t *end = p + text.len;
    while (p < end && olen < 64) {
        uint8_t c = *p;
        if (__builtin_expect(c < 0x80, 1)) {
            p++;
            uint8_t keep = SLUG_ASCII[c];
            if (!keep) {
                if (olen > 0) gap = 1;
                continue;
            }
            if (gap) { tmp[olen++] = '-'; gap = 0; if (olen >= 64) break; }
            tmp[olen++] = (char)keep;
        } else {
            size_t st;
            uint32_t cp = decode_utf8_one(p, end, &st);
            p += st;
            if (is_mark_cp(cp)) continue; // dropped without setting gap!
            int idx = lookup_slug_map(cp);
            if (idx < 0) {
                if (olen > 0) gap = 1;
                continue;
            }
            const char *seq = SLUG_MAP_POOL + SLUG_MAP_OFF[idx];
            uint32_t slen = SLUG_MAP_LEN_ARR[idx];
            for (uint32_t j = 0; j < slen && olen < 64; j++) {
                char sc = seq[j];
                if (sc == '-') {
                    if (olen > 0) gap = 1;
                } else {
                    if (gap) { tmp[olen++] = '-'; gap = 0; if (olen >= 64) break; }
                    tmp[olen++] = sc;
                }
            }
        }
    }
    if (olen > 64) olen = 64;
    while (olen > 0 && tmp[olen - 1] == '-') olen--;
    if (olen == 0) return STR_LIT("untitled");
    char *dst = arena_alloc_str(a, olen);
    fast_copy(dst, tmp, olen);
    return (Str){ dst, olen };
}

// ---- Exact longDate("YYYY-MM-DD") -> "9 November 2026" ----
static const char *MONTH_NAMES[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
static const uint8_t MONTH_LENS[12] = { 7, 8, 5, 5, 3, 4, 4, 6, 9, 7, 8, 8 };

static Str long_date(Arena *a, Str date) {
    if (date.len != 10) return STR_EMPTY;
    int mo = (date.p[5] - '0') * 10 + (date.p[6] - '0');
    int d = (date.p[8] - '0') * 10 + (date.p[9] - '0');
    if (mo < 1 || mo > 12) return STR_EMPTY;
    char *dst = arena_alloc_str(a, 24);
    uint32_t n = 0;
    if (d >= 10) dst[n++] = (char)('0' + d / 10);
    dst[n++] = (char)('0' + d % 10);
    dst[n++] = ' ';
    uint8_t ml = MONTH_LENS[mo - 1];
    fast_copy(dst + n, MONTH_NAMES[mo - 1], ml);
    n += ml;
    dst[n++] = ' ';
    dst[n++] = date.p[0];
    dst[n++] = date.p[1];
    dst[n++] = date.p[2];
    dst[n++] = date.p[3];
    // trim arena back to exact length
    a->pos -= (24 - n);
    return (Str){ dst, n };
}

// ---- Exact JS \s whitespace check (ASCII + Unicode whitespace) ----
static inline int is_js_ws_cp(uint32_t cp) {
    if (cp <= 0x20) return cp == ' ' || (cp >= 0x09 && cp <= 0x0D);
    if (cp == 0xA0 || cp == 0x1680 || cp == 0xFEFF) return 1;
    if (cp >= 0x2000 && cp <= 0x200A) return 1;
    if (cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000) return 1;
    return 0;
}

static inline Str js_trim(Str s) {
    const uint8_t *p = (const uint8_t *)s.p;
    const uint8_t *end = p + s.len;
    while (p < end) {
        if (*p < 0x80) {
            if (*p == ' ' || (*p >= 0x09 && *p <= 0x0D)) { p++; continue; }
            break;
        }
        size_t st; uint32_t cp = decode_utf8_one(p, end, &st);
        if (is_js_ws_cp(cp)) { p += st; continue; }
        break;
    }
    while (end > p) {
        uint8_t last = end[-1];
        if (last < 0x80) {
            if (last == ' ' || (last >= 0x09 && last <= 0x0D)) { end--; continue; }
            break;
        }
        // Back up to UTF-8 lead byte
        const uint8_t *q = end - 1;
        while (q > p && (*q & 0xC0) == 0x80) q--;
        size_t st; uint32_t cp = decode_utf8_one(q, end, &st);
        if (is_js_ws_cp(cp)) { end = q; continue; }
        break;
    }
    return (Str){ (const char *)p, (uint32_t)(end - p) };
}

// Count words (\S+ runs) in body for reading_minutes (SIMD fast path + exact UTF-8 fallback!)
static uint32_t count_words(Str text) {
    int64_t fast = count_words_fast_simd((const uint8_t *)text.p, text.len);
    if (fast >= 0) return (uint32_t)fast;
    const uint8_t *p = (const uint8_t *)text.p;
    const uint8_t *end = p + text.len;
    uint32_t words = 0;
    int in_word = 0;
    while (p < end) {
        uint8_t c = *p;
        if (c < 0x80) {
            p++;
            int ws = (c == ' ' || (c >= 0x09 && c <= 0x0D));
            if (!ws && !in_word) { words++; in_word = 1; }
            else if (ws) { in_word = 0; }
        } else {
            size_t st; uint32_t cp = decode_utf8_one(p, end, &st);
            p += st;
            int ws = is_js_ws_cp(cp);
            if (!ws && !in_word) { words++; in_word = 1; }
            else if (ws) { in_word = 0; }
        }
    }
    return words;
}

// ---- Exact Front Matter Parser ----
typedef struct {
    Str title;
    Str date;
    Str summary;
    Str layout;
    Str slug;
    Str *tags;
    uint32_t num_tags;
    uint8_t has_title, has_date, has_summary, has_layout, has_slug, has_tags, has_draft;
    uint8_t draft;
    Str body;
    Str error; // non-empty if refused
} FrontMatter;

static inline int days_in_month(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

static Str fmt_fm_error(Arena *a, const char *s1, Str s2, const char *s3) {
    size_t l1 = 0; while (s1[l1]) l1++;
    size_t l3 = 0; while (s3[l3]) l3++;
    char *dst = arena_alloc_str(a, l1 + s2.len + l3);
    fast_copy(dst, s1, l1);
    fast_copy(dst + l1, s2.p, s2.len);
    fast_copy(dst + l1 + s2.len, s3, l3);
    return (Str){ dst, (uint32_t)(l1 + s2.len + l3) };
}

static Str fm_parse_string(Arena *a, Str key, Str raw, Str *err) {
    if (raw.len > 0 && raw.p[0] == '"') {
        if (raw.len < 2 || raw.p[raw.len - 1] != '"') {
            *err = fmt_fm_error(a, "key \"", key, "\": a quoted string is not closed");
            return STR_EMPTY;
        }
        char *out = arena_alloc_str(a, raw.len);
        uint32_t olen = 0;
        for (uint32_t i = 1; i + 1 < raw.len; i++) {
            char c = raw.p[i];
            if (c == '\\') {
                char n = raw.p[i + 1];
                if ((n != '"' && n != '\\') || i + 1 == raw.len - 1) {
                    *err = fmt_fm_error(a, "key \"", key, "\": a quoted string holds an escape other than \\\" or \\\\");
                    return STR_EMPTY;
                }
                out[olen++] = n;
                i++;
            } else if (c == '"') {
                *err = fmt_fm_error(a, "key \"", key, "\": a quoted string holds a stray quote");
                return STR_EMPTY;
            } else {
                out[olen++] = c;
            }
        }
        return (Str){ out, olen };
    }
    if (raw.len > 0 && raw.p[0] == '[') {
        *err = fmt_fm_error(a, "key \"", key, "\": a bare string opens on [");
        return STR_EMPTY;
    }
    return raw;
}

static void parse_front_matter(Arena *a, Str text, FrontMatter *fm) {
    memset(fm, 0, sizeof(*fm));
    const uint8_t *p = (const uint8_t *)text.p;
    const uint8_t *end = p + text.len;
    const uint8_t *nl = find_nl(p, end);
    if ((nl - p) != 3 || p[0] != '-' || p[1] != '-' || p[2] != '-') {
        fm->error = STR_LIT("no front matter");
        return;
    }
    if (nl == end) {
        fm->error = STR_LIT("the front matter is not closed");
        return;
    }
    const uint8_t *fm_start = nl + 1;
    // Find closing --- line
    const uint8_t *cur = fm_start;
    const uint8_t *close_line = NULL;
    const uint8_t *close_nl = NULL;
    while (cur <= end) {
        const uint8_t *lnl = find_nl(cur, end);
        if ((lnl - cur) == 3 && cur[0] == '-' && cur[1] == '-' && cur[2] == '-') {
            close_line = cur;
            close_nl = lnl;
            break;
        }
        if (lnl == end) break;
        cur = lnl + 1;
    }
    if (!close_line) {
        fm->error = STR_LIT("the front matter is not closed");
        return;
    }
    // Parse lines between fm_start and close_line
    cur = fm_start;
    uint32_t line_no = 2; // 1-based; opening --- is line 1
    while (cur < close_line) {
        const uint8_t *lnl = find_nl(cur, close_line);
        size_t llen = (size_t)(lnl - cur);
        // Check blank (spaces/tabs only) or comment (#)
        if (llen > 0 && cur[0] == '#') { cur = lnl + 1; line_no++; continue; }
        size_t k = 0;
        while (k < llen && (cur[k] == ' ' || cur[k] == '\t')) k++;
        if (k == llen) { cur = lnl + 1; line_no++; continue; }

        // Match ^([a-z_]+):(?: (.*))?$
        size_t ki = 0;
        while (ki < llen && ((cur[ki] >= 'a' && cur[ki] <= 'z') || cur[ki] == '_')) ki++;
        int ok_line = (ki > 0 && ki < llen && cur[ki] == ':' && (ki + 1 == llen || cur[ki + 1] == ' '));
        if (!ok_line) {
            char *dst = arena_alloc_str(a, 48);
            Buf b = { dst, 0, 48, -1 };
            buf_lit(&b, "line "); buf_put_u32(&b, line_no); buf_lit(&b, " is not \"key: value\"");
            fm->error = (Str){ dst, (uint32_t)b.len };
            return;
        }
        Str key = { (const char *)cur, (uint32_t)ki };
        const uint8_t *val_p = (ki + 1 < llen) ? (cur + ki + 2) : (cur + llen);
        const uint8_t *val_e = lnl;
        while (val_e > val_p && (val_e[-1] == ' ' || val_e[-1] == '\t')) val_e--;
        Str raw = { (const char *)val_p, (uint32_t)(val_e - val_p) };

        int kid = -1;
        if (STR_EQ(key, "title")) kid = 0;
        else if (STR_EQ(key, "date")) kid = 1;
        else if (STR_EQ(key, "tags")) kid = 2;
        else if (STR_EQ(key, "draft")) kid = 3;
        else if (STR_EQ(key, "layout")) kid = 4;
        else if (STR_EQ(key, "slug")) kid = 5;
        else if (STR_EQ(key, "summary")) kid = 6;
        if (kid < 0) {
            fm->error = fmt_fm_error(a, "unknown key \"", key, "\"");
            return;
        }
        uint8_t *has_ptr[7] = { &fm->has_title, &fm->has_date, &fm->has_tags, &fm->has_draft, &fm->has_layout, &fm->has_slug, &fm->has_summary };
        if (*has_ptr[kid]) {
            fm->error = fmt_fm_error(a, "key \"", key, "\" is given twice");
            return;
        }
        if (raw.len == 0) {
            fm->error = fmt_fm_error(a, "key \"", key, "\" has no value");
            return;
        }
        switch (kid) {
            case 0: { // title
                Str v = fm_parse_string(a, key, raw, &fm->error);
                if (fm->error.len) return;
                fm->title = v; fm->has_title = 1;
                break;
            }
            case 6: { // summary
                Str v = fm_parse_string(a, key, raw, &fm->error);
                if (fm->error.len) return;
                fm->summary = v; fm->has_summary = 1;
                break;
            }
            case 1: { // date
                int valid = (raw.len == 10 && raw.p[4] == '-' && raw.p[7] == '-');
                for (int j = 0; valid && j < 10; j++) {
                    if (j == 4 || j == 7) continue;
                    if (raw.p[j] < '0' || raw.p[j] > '9') valid = 0;
                }
                if (valid) {
                    int y = (raw.p[0]-'0')*1000 + (raw.p[1]-'0')*100 + (raw.p[2]-'0')*10 + (raw.p[3]-'0');
                    int mo = (raw.p[5]-'0')*10 + (raw.p[6]-'0');
                    int d = (raw.p[8]-'0')*10 + (raw.p[9]-'0');
                    if (y < 1970 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo)) valid = 0;
                }
                if (!valid) {
                    fm->error = STR_LIT("key \"date\": not a date from 1970-01-01 to 2099-12-31 (YYYY-MM-DD)");
                    return;
                }
                fm->date = raw; fm->has_date = 1;
                break;
            }
            case 2: { // tags
                if (raw.len < 2 || raw.p[0] != '[' || raw.p[raw.len - 1] != ']') {
                    fm->error = STR_LIT("key \"tags\": not a list [a, b]");
                    return;
                }
                Str inner = { raw.p + 1, raw.len - 2 };
                Str trimmed = js_trim(inner);
                if (trimmed.len == 0) {
                    fm->tags = NULL; fm->num_tags = 0; fm->has_tags = 1;
                    break;
                }
                // Count commas
                uint32_t cnt = 1;
                for (uint32_t j = 0; j < inner.len; j++) if (inner.p[j] == ',') cnt++;
                Str *tarr = (Str *)arena_alloc(a, cnt * sizeof(Str), 8);
                uint32_t idx = 0;
                uint32_t st = 0;
                for (uint32_t j = 0; j <= inner.len; j++) {
                    if (j == inner.len || inner.p[j] == ',') {
                        Str item = js_trim((Str){ inner.p + st, j - st });
                        if (item.len == 0) {
                            fm->error = STR_LIT("key \"tags\": an empty tag, or one holding \" [ or ]");
                            return;
                        }
                        for (uint32_t u = 0; u < item.len; u++) {
                            char ch = item.p[u];
                            if (ch == '"' || ch == '[' || ch == ']') {
                                fm->error = STR_LIT("key \"tags\": an empty tag, or one holding \" [ or ]");
                                return;
                            }
                        }
                        tarr[idx++] = item;
                        st = j + 1;
                    }
                }
                fm->tags = tarr; fm->num_tags = idx; fm->has_tags = 1;
                break;
            }
            case 3: { // draft
                if (STR_EQ(raw, "true")) { fm->draft = 1; fm->has_draft = 1; }
                else if (STR_EQ(raw, "false")) { fm->draft = 0; fm->has_draft = 1; }
                else { fm->error = STR_LIT("key \"draft\": true or false"); return; }
                break;
            }
            case 4: { // layout: ^[a-z0-9-]+$
                for (uint32_t j = 0; j < raw.len; j++) {
                    char ch = raw.p[j];
                    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
                        fm->error = STR_LIT("key \"layout\": not a template name");
                        return;
                    }
                }
                fm->layout = raw; fm->has_layout = 1;
                break;
            }
            case 5: { // slug: ^[a-z0-9]+(?:-[a-z0-9]+)*$ and len <= 64
                int ok = (raw.len >= 1 && raw.len <= 64);
                int prev_dash = 1;
                for (uint32_t j = 0; ok && j < raw.len; j++) {
                    char ch = raw.p[j];
                    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) prev_dash = 0;
                    else if (ch == '-' && !prev_dash && j + 1 < raw.len) prev_dash = 1;
                    else ok = 0;
                }
                if (!ok) { fm->error = STR_LIT("key \"slug\": not a slug"); return; }
                fm->slug = raw; fm->has_slug = 1;
                break;
            }
        }
        cur = lnl + 1;
        line_no++;
    }
    if (!fm->has_title) {
        fm->error = STR_LIT("no title");
        return;
    }
    if (close_nl < end) {
        fm->body = (Str){ (const char *)(close_nl + 1), (uint32_t)(end - (close_nl + 1)) };
    } else {
        fm->body = STR_EMPTY;
    }
}
