#pragma once
#include "sys_simd.h"
#include "text_fm.h"
#include "markdown.h"

// ---- Site Data Model exposed to Templates ----
typedef struct TagObj TagObj;
typedef struct PageObj PageObj;

typedef struct {
    Str name, name_ej, name_rj;
    Str slug, slug_ej, slug_rj;
    Str url, url_ej, url_rj;
    TagObj *global_tag; // NULL if page-only orphan tag
} TagRef;

struct TagObj {
    Str name, name_ej, name_rj;
    Str slug, slug_ej, slug_rj;
    Str url, url_ej, url_rj;
    PageObj **posts;
    uint32_t num_posts;
    uint32_t cap_posts;
    uint32_t idx;
};

struct PageObj {
    Str src_path;
    Str text;
    Str body;
    Str layout;
    Str slug, slug_ej, slug_rj;
    Str out_dir;
    Str url, url_ej, url_rj;
    Str title, title_ej, title_rj;
    Str date, date_ej, date_rj;
    Str date_long, date_long_ej, date_long_rj;
    Str summary, summary_ej, summary_rj;
    Str html;      // JSON-escaped if direct_json_md, else raw
    Str excerpt;   // JSON-escaped if direct_json_md, else raw
    TocItem *toc;
    uint32_t num_toc;
    TagRef *tag_refs;
    uint32_t num_tag_refs;
    Str *raw_tags;
    uint32_t num_raw_tags;
    PageObj *newer;
    PageObj *older;
    uint32_t reading_minutes;
    uint32_t post_idx; // index in b->posts (for memoized cards)
    uint8_t is_post;
    uint8_t has_date;
};

typedef struct {
    uint32_t number;
    uint32_t total;
    Str url, url_ej, url_rj;
    Str newer_url, newer_url_ej, newer_url_rj;
    Str older_url, older_url_ej, older_url_rj;
} PagerObj;

typedef struct {
    Str title, title_ej, title_rj;
    Str base_url, base_url_ej, base_url_rj;
} SiteObj;

// ---- Template AST ----
#define TPL_INCLUDE_DEPTH 8

enum {
    NK_TEXT = 0,     // raw unescaped text (pre-compile)
    NK_LIT_JSON = 1, // pre-JSON-escaped literal bytes (post-compile!)
    NK_VAR = 2,      // {{ path }}
    NK_RAW = 3,      // {{{ path }}}
    NK_IF = 4,       // {% if [not] path %}
    NK_FOR = 5,      // {% for x in path %}
    NK_INCLUDE = 6,  // {% include "name" %}
    NK_MEMO_FOR = 7, // specialized memoized post-card loop!
};

typedef struct TNode TNode;

typedef struct {
    TNode *items;
    uint32_t len;
    uint32_t cap;
} TNodeList;

typedef struct {
    Str segs[8];
    uint8_t nsegs;
} TPath;

enum {
    FOP_NONE = 0,
    // Page direct emit / truthy ops (when not shadowed by loop)
    FOP_PAGE_TITLE_EJ, FOP_PAGE_URL_EJ, FOP_PAGE_DATE, FOP_PAGE_DATE_LONG,
    FOP_PAGE_SUMMARY_EJ, FOP_PAGE_EXCERPT, FOP_PAGE_CONTENT, FOP_PAGE_RMIN,
    FOP_PAGE_SLUG, FOP_PAGE_TAGS, FOP_PAGE_TOC, FOP_PAGE_NEWER, FOP_PAGE_OLDER,
    FOP_PAGE_NEWER_URL, FOP_PAGE_NEWER_TITLE, FOP_PAGE_OLDER_URL, FOP_PAGE_OLDER_TITLE,
    // Tag direct ops
    FOP_TAG_NAME_EJ, FOP_TAG_SLUG, FOP_TAG_URL_EJ, FOP_TAG_COUNT, FOP_TAG_POSTS,
    // Pager direct ops
    FOP_PAGER_NUM, FOP_PAGER_TOTAL, FOP_PAGER_URL_EJ, FOP_PAGER_NEWER_EJ, FOP_PAGER_OLDER_EJ,
    // Top-of-stack loop item ops (frame[depth-1])
    FOP_L0_TITLE_EJ, FOP_L0_URL_EJ, FOP_L0_DATE, FOP_L0_DATE_LONG,
    FOP_L0_SUMMARY_EJ, FOP_L0_EXCERPT, FOP_L0_RMIN, FOP_L0_TAGS,
    FOP_L0_NAME_EJ, FOP_L0_SLUG, FOP_L0_LEVEL, FOP_L0_ID, FOP_L0_HTML,
    FOP_LOOP_INDEX, FOP_LOOP_FIRST, FOP_LOOP_LAST, FOP_LOOP_LENGTH,
};

struct TNode {
    uint8_t kind;
    uint8_t neg;      // for NK_IF
    uint8_t fast_op;  // pre-resolved opcode!
    uint16_t memo_id; // for NK_MEMO_FOR
    Str text;         // NK_TEXT, NK_LIT_JSON, NK_INCLUDE name, NK_FOR var name
    TPath path;       // NK_VAR, NK_RAW, NK_IF, NK_FOR
    TNodeList body;   // NK_IF then, NK_FOR body
    TNodeList els;    // NK_IF else
    uint8_t has_else;
};

static inline void nlist_push(Arena *a, TNodeList *nl, TNode node) {
    if (nl->len == nl->cap) {
        uint32_t ncap = nl->cap ? nl->cap * 2 : 8;
        TNode *ni = (TNode *)arena_alloc(a, ncap * sizeof(TNode), 8);
        if (nl->len) memcpy(ni, nl->items, nl->len * sizeof(TNode));
        nl->items = ni;
        nl->cap = ncap;
    }
    nl->items[nl->len++] = node;
}

static inline int is_ident_start(uint8_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static inline int is_ident_char(uint8_t c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static Str fmt_tpl_err(Arena *a, Str name, const char *mid, Str arg, const char *tail) {
    size_t ml = 0; while (mid[ml]) ml++;
    size_t tl = 0; while (tail[tl]) tl++;
    char *dst = arena_alloc_str(a, 12 + name.len + ml + arg.len + tl + 16);
    Buf b = { dst, 0, 0, -1 };
    buf_lit(&b, "template \""); buf_put_str(&b, name); buf_lit(&b, "\": ");
    buf_puts(&b, mid, ml);
    buf_put_str(&b, arg);
    buf_puts(&b, tail, tl);
    return (Str){ dst, (uint32_t)b.len };
}

static Str fmt_tpl_path_err(Arena *a, Str name, Str text) {
    // template "<name>": <JSON.stringify(text)> is not a path
    char *dst = arena_alloc_str(a, 32 + name.len + text.len * 6 + 32);
    Buf b = { dst, 0, 0, -1 };
    buf_lit(&b, "template \""); buf_put_str(&b, name); buf_lit(&b, "\": \"");
    buf_append_json_escaped(&b, text.p, text.len);
    buf_lit(&b, "\" is not a path");
    return (Str){ dst, (uint32_t)b.len };
}

static int parse_tpath(Arena *a, Str tname, Str text, TPath *out, Str *err) {
    out->nsegs = 0;
    if (text.len == 0) { *err = fmt_tpl_path_err(a, tname, text); return 0; }
    uint32_t i = 0;
    while (i < text.len) {
        if (!is_ident_start((uint8_t)text.p[i])) { *err = fmt_tpl_path_err(a, tname, text); return 0; }
        uint32_t st = i++;
        while (i < text.len && is_ident_char((uint8_t)text.p[i])) i++;
        if (out->nsegs < 8) out->segs[out->nsegs++] = (Str){ text.p + st, i - st };
        if (i < text.len) {
            if (text.p[i] != '.' || i + 1 == text.len) { *err = fmt_tpl_path_err(a, tname, text); return 0; }
            i++;
        }
    }
    return 1;
}

typedef struct {
    TNodeList nodes;
    int uses_escaped_html_field; // 1 if {{ *.content }}, {{ *.excerpt }}, {{ *.html }} appears
} ParsedTemplate;

static int parse_template(Arena *a, Str name, Str src, ParsedTemplate *out, Str *err) {
    memset(out, 0, sizeof(*out));
    *err = STR_EMPTY;

    // Parse directly with explicit block stack
    typedef struct {
        uint8_t kind; // 0=root, 1=for, 2=if
        uint8_t in_else;
        TNodeList *target;
        TNode node;
    } Frame;
    Frame stack[64];
    int sp = 0;
    stack[0].kind = 0;
    stack[0].in_else = 0;
    stack[0].target = &out->nodes;

    // Accumulate current text chunk
    char *tbuf = arena_alloc_str(a, src.len + 16);
    uint32_t tlen = 0;

    uint32_t i = 0;
    while (i < src.len) {
        // Scan for '{'
        uint32_t open = i;
        while (open < src.len && src.p[open] != '{') open++;
        if (open >= src.len || open + 1 >= src.len) {
            fast_copy(tbuf + tlen, src.p + i, src.len - i);
            tlen += src.len - i;
            break;
        }
        char nxt = src.p[open + 1];
        if (nxt != '{' && nxt != '%') {
            fast_copy(tbuf + tlen, src.p + i, (open + 1) - i);
            tlen += (open + 1) - i;
            i = open + 1;
            continue;
        }
        if (open > i) {
            fast_copy(tbuf + tlen, src.p + i, open - i);
            tlen += open - i;
        }
        if (nxt == '{') {
            int raw = (open + 2 < src.len && src.p[open + 2] == '{');
            uint32_t dlen = raw ? 3 : 2;
            // Find }} or }}}
            int32_t close = -1;
            for (uint32_t j = open + dlen; j + dlen <= src.len; j++) {
                if (src.p[j] == '}' && src.p[j + 1] == '}' && (!raw || src.p[j + 2] == '}')) {
                    close = (int32_t)j;
                    break;
                }
            }
            if (close < 0) {
                *err = fmt_tpl_err(a, name, raw ? "a {{{ is not closed" : "a {{ is not closed", STR_EMPTY, "");
                return 0;
            }
            if (tlen > 0) {
                char *copy = arena_alloc_str(a, tlen);
                fast_copy(copy, tbuf, tlen);
                TNode tn = {0}; tn.kind = NK_TEXT; tn.text = (Str){ copy, tlen };
                nlist_push(a, stack[sp].target, tn);
                tlen = 0;
            }
            Str inner = js_trim((Str){ src.p + open + dlen, (uint32_t)close - (open + dlen) });
            TNode vn = {0};
            vn.kind = raw ? NK_RAW : NK_VAR;
            if (!parse_tpath(a, name, inner, &vn.path, err)) return 0;
            if (!raw && vn.path.nsegs >= 1) {
                Str last = vn.path.segs[vn.path.nsegs - 1];
                if (STR_EQ(last, "content") || STR_EQ(last, "excerpt") || STR_EQ(last, "html")) {
                    out->uses_escaped_html_field = 1;
                }
            }
            nlist_push(a, stack[sp].target, vn);
            i = (uint32_t)close + dlen;
            continue;
        }
        // '{%'
        int32_t close = -1;
        for (uint32_t j = open + 2; j + 1 < src.len; j++) {
            if (src.p[j] == '%' && src.p[j + 1] == '}') { close = (int32_t)j; break; }
        }
        if (close < 0) {
            *err = fmt_tpl_err(a, name, "a {% is not closed", STR_EMPTY, "");
            return 0;
        }
        uint32_t end = (uint32_t)close + 2;
        // Standalone line check
        uint32_t line_start = open;
        while (line_start > 0 && src.p[line_start - 1] != '\n') line_start--;
        uint32_t nl = end;
        while (nl < src.len && src.p[nl] != '\n') nl++;
        int standalone = 1;
        for (uint32_t k = line_start; k < open; k++) if (src.p[k] != ' ' && src.p[k] != '\t') { standalone = 0; break; }
        for (uint32_t k = end; standalone && k < nl; k++) if (src.p[k] != ' ' && src.p[k] != '\t') { standalone = 0; break; }
        if (standalone) {
            uint32_t cut = open - line_start;
            if (cut <= tlen) tlen -= cut; else tlen = 0;
        }
        if (tlen > 0) {
            char *copy = arena_alloc_str(a, tlen);
            fast_copy(copy, tbuf, tlen);
            TNode tn = {0}; tn.kind = NK_TEXT; tn.text = (Str){ copy, tlen };
            nlist_push(a, stack[sp].target, tn);
            tlen = 0;
        }
        Str tag = js_trim((Str){ src.p + open + 2, (uint32_t)close - (open + 2) });
        i = standalone ? (nl == src.len ? src.len : nl + 1) : end;

        // Classify tag
        if (tag.len > 4 && memcmp(tag.p, "for ", 4) == 0) {
            // ^for ([A-Za-z_][A-Za-z0-9_]*) in (\S+)$
            uint32_t k = 4;
            if (k < tag.len && is_ident_start((uint8_t)tag.p[k])) {
                uint32_t vst = k++;
                while (k < tag.len && is_ident_char((uint8_t)tag.p[k])) k++;
                Str vname = { tag.p + vst, k - vst };
                if (k + 4 <= tag.len && memcmp(tag.p + k, " in ", 4) == 0) {
                    Str pstr = { tag.p + k + 4, tag.len - (k + 4) };
                    if (pstr.len > 0 && !has_js_ws(pstr.p, pstr.len)) {
                        TNode fn = {0}; fn.kind = NK_FOR; fn.text = vname;
                        if (!parse_tpath(a, name, pstr, &fn.path, err)) return 0;
                        sp++;
                        stack[sp].kind = 1;
                        stack[sp].in_else = 0;
                        stack[sp].node = fn;
                        stack[sp].target = &stack[sp].node.body;
                        continue;
                    }
                }
            }
            *err = fmt_tpl_err(a, name, "{% ", tag, " %} is not a tag");
            return 0;
        }
        if (tag.len > 3 && memcmp(tag.p, "if ", 3) == 0) {
            uint32_t k = 3;
            int neg = 0;
            if (k + 4 <= tag.len && memcmp(tag.p + k, "not ", 4) == 0) { neg = 1; k += 4; }
            Str pstr = { tag.p + k, tag.len - k };
            if (pstr.len > 0 && !has_js_ws(pstr.p, pstr.len)) {
                TNode in = {0}; in.kind = NK_IF; in.neg = (uint8_t)neg;
                if (!parse_tpath(a, name, pstr, &in.path, err)) return 0;
                sp++;
                stack[sp].kind = 2;
                stack[sp].in_else = 0;
                stack[sp].node = in;
                stack[sp].target = &stack[sp].node.body;
                continue;
            }
            *err = fmt_tpl_err(a, name, "{% ", tag, " %} is not a tag");
            return 0;
        }
        if (STR_EQ(tag, "else")) {
            if (stack[sp].kind != 2 || stack[sp].in_else) {
                *err = fmt_tpl_err(a, name, "an else outside an if", STR_EMPTY, "");
                return 0;
            }
            stack[sp].in_else = 1;
            stack[sp].node.has_else = 1;
            stack[sp].target = &stack[sp].node.els;
            continue;
        }
        if (STR_EQ(tag, "endfor")) {
            if (stack[sp].kind != 1) {
                *err = fmt_tpl_err(a, name, "an endfor with no open for", STR_EMPTY, "");
                return 0;
            }
            TNode done = stack[sp].node;
            sp--;
            nlist_push(a, stack[sp].target, done);
            continue;
        }
        if (STR_EQ(tag, "endif")) {
            if (stack[sp].kind != 2) {
                *err = fmt_tpl_err(a, name, "an endif with no open if", STR_EMPTY, "");
                return 0;
            }
            TNode done = stack[sp].node;
            sp--;
            nlist_push(a, stack[sp].target, done);
            continue;
        }
        if (tag.len > 10 && memcmp(tag.p, "include \"", 9) == 0 && tag.p[tag.len - 1] == '"') {
            Str inc = { tag.p + 9, tag.len - 10 };
            int ok = inc.len > 0;
            for (uint32_t k = 0; ok && k < inc.len; k++) {
                char c = inc.p[k];
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) ok = 0;
            }
            if (ok) {
                TNode ic = {0}; ic.kind = NK_INCLUDE; ic.text = inc;
                nlist_push(a, stack[sp].target, ic);
                continue;
            }
        }
        *err = fmt_tpl_err(a, name, "{% ", tag, " %} is not a tag");
        return 0;
    }
    if (tlen > 0) {
        char *copy = arena_alloc_str(a, tlen);
        fast_copy(copy, tbuf, tlen);
        TNode tn = {0}; tn.kind = NK_TEXT; tn.text = (Str){ copy, tlen };
        nlist_push(a, stack[sp].target, tn);
    }
    if (sp > 0) {
        *err = fmt_tpl_err(a, name, stack[sp].kind == 1 ? "a for is not closed" : "a if is not closed", STR_EMPTY, "");
        return 0;
    }
    return 1;
}

// ---- Runtime Value Representation ----
enum {
    VK_UNDEF = 0,
    VK_NULL,
    VK_BOOL,
    VK_INT,
    VK_STR_RAW,   // raw string (needs escaping on emit)
    VK_STR_EJ_RJ, // pair: ej (HTML+JSON escaped) and rj (raw JSON escaped)
    VK_STR_MD,    // markdown HTML (already JSON-escaped if direct_json_md, else raw HTML)
    VK_SITE,
    VK_PAGE,      // full page
    VK_CARD,      // post card (subset of PageObj, no slug/content/toc/newer/older/is_post)
    VK_LINK,      // newer/older link (title/url/date/date_long)
    VK_TAG_FULL,  // tag with posts (tag page)
    VK_TAG_BRIEF, // tag with count, no posts (site.tags)
    VK_TAG_REF,   // tag with name/slug/url only (card.tags)
    VK_TOC_ITEM,
    VK_PAGER,
    VK_LOOP,
    VK_ARR_TAGS,      // TagObj* array (allTags)
    VK_ARR_TAG_REFS,  // TagRef* array (card/page tags)
    VK_ARR_CARDS,     // PageObj** array (posts / recent / tag.posts)
    VK_ARR_TOC,       // TocItem* array
};

typedef struct {
    uint32_t index;
    uint32_t length;
    uint8_t first;
    uint8_t last;
} LoopObj;

typedef struct {
    uint8_t kind;
    uint8_t bval;
    uint32_t uval;
    Str s1, s2; // for VK_STR_EJ_RJ: s1=ej, s2=rj; for VK_STR_RAW/MD: s1=str
    const void *ptr;
} TVal;

static inline TVal val_undef(void) { TVal v = {0}; v.kind = VK_UNDEF; return v; }
static inline TVal val_null(void) { TVal v = {0}; v.kind = VK_NULL; return v; }
static inline TVal val_bool(int b) { TVal v = {0}; v.kind = VK_BOOL; v.bval = (uint8_t)(b ? 1 : 0); return v; }
static inline TVal val_u32(uint32_t u) { TVal v = {0}; v.kind = VK_INT; v.uval = u; return v; }
static inline TVal val_ej_rj(Str ej, Str rj, Str raw) {
    TVal v = {0}; v.kind = VK_STR_EJ_RJ; v.s1 = ej; v.s2 = rj; v.uval = raw.len; return v;
}
static inline TVal val_md(Str s) { TVal v = {0}; v.kind = VK_STR_MD; v.s1 = s; return v; }

static inline int val_truthy(TVal v) {
    switch (v.kind) {
        case VK_UNDEF: case VK_NULL: return 0;
        case VK_BOOL: return v.bval != 0;
        case VK_INT: return v.uval != 0;
        case VK_STR_RAW: return v.s1.len != 0;
        case VK_STR_EJ_RJ: return v.uval != 0; // original raw length!
        case VK_STR_MD: return v.s1.len != 0;
        case VK_ARR_TAGS: case VK_ARR_TAG_REFS: case VK_ARR_CARDS: case VK_ARR_TOC:
            return v.uval != 0;
        default: return 1; // objects are truthy!
    }
}

static TVal val_prop(TVal obj, Str prop) {
    switch (obj.kind) {
        case VK_SITE: {
            const SiteObj *s = (const SiteObj *)obj.ptr;
            if (STR_EQ(prop, "title")) return val_ej_rj(s->title_ej, s->title_rj, s->title);
            if (STR_EQ(prop, "base_url")) return val_ej_rj(s->base_url_ej, s->base_url_rj, s->base_url);
            return val_undef();
        }
        case VK_PAGE:
        case VK_CARD: {
            const PageObj *p = (const PageObj *)obj.ptr;
            if (STR_EQ(prop, "title")) return val_ej_rj(p->title_ej, p->title_rj, p->title);
            if (STR_EQ(prop, "url")) return val_ej_rj(p->url_ej, p->url_rj, p->url);
            if (STR_EQ(prop, "date")) return val_ej_rj(p->date_ej, p->date_rj, p->date);
            if (STR_EQ(prop, "date_long")) return val_ej_rj(p->date_long_ej, p->date_long_rj, p->date_long);
            if (STR_EQ(prop, "summary")) return val_ej_rj(p->summary_ej, p->summary_rj, p->summary);
            if (STR_EQ(prop, "excerpt")) return val_md(p->excerpt);
            if (STR_EQ(prop, "reading_minutes")) return val_u32(p->reading_minutes);
            if (STR_EQ(prop, "tags")) {
                TVal v = {0}; v.kind = VK_ARR_TAG_REFS; v.ptr = p->tag_refs; v.uval = p->num_tag_refs; return v;
            }
            if (obj.kind == VK_PAGE) {
                if (STR_EQ(prop, "slug")) return val_ej_rj(p->slug_ej, p->slug_rj, p->slug);
                if (STR_EQ(prop, "content")) return val_md(p->html);
                if (STR_EQ(prop, "toc")) {
                    TVal v = {0}; v.kind = VK_ARR_TOC; v.ptr = p->toc; v.uval = p->num_toc; return v;
                }
                if (STR_EQ(prop, "newer")) {
                    if (!p->newer) return val_null();
                    TVal v = {0}; v.kind = VK_LINK; v.ptr = p->newer; return v;
                }
                if (STR_EQ(prop, "older")) {
                    if (!p->older) return val_null();
                    TVal v = {0}; v.kind = VK_LINK; v.ptr = p->older; return v;
                }
                if (STR_EQ(prop, "is_post")) return val_bool(p->is_post);
            }
            return val_undef();
        }
        case VK_LINK: {
            const PageObj *p = (const PageObj *)obj.ptr;
            if (STR_EQ(prop, "title")) return val_ej_rj(p->title_ej, p->title_rj, p->title);
            if (STR_EQ(prop, "url")) return val_ej_rj(p->url_ej, p->url_rj, p->url);
            if (STR_EQ(prop, "date")) return val_ej_rj(p->date_ej, p->date_rj, p->date);
            if (STR_EQ(prop, "date_long")) return val_ej_rj(p->date_long_ej, p->date_long_rj, p->date_long);
            return val_undef();
        }
        case VK_TAG_FULL:
        case VK_TAG_BRIEF: {
            const TagObj *t = (const TagObj *)obj.ptr;
            if (STR_EQ(prop, "name")) return val_ej_rj(t->name_ej, t->name_rj, t->name);
            if (STR_EQ(prop, "slug")) return val_ej_rj(t->slug_ej, t->slug_rj, t->slug);
            if (STR_EQ(prop, "url")) return val_ej_rj(t->url_ej, t->url_rj, t->url);
            if (STR_EQ(prop, "count")) return val_u32(t->num_posts);
            if (obj.kind == VK_TAG_FULL && STR_EQ(prop, "posts")) {
                TVal v = {0}; v.kind = VK_ARR_CARDS; v.ptr = t->posts; v.uval = t->num_posts; return v;
            }
            return val_undef();
        }
        case VK_TAG_REF: {
            const TagRef *t = (const TagRef *)obj.ptr;
            if (STR_EQ(prop, "name")) return val_ej_rj(t->name_ej, t->name_rj, t->name);
            if (STR_EQ(prop, "slug")) return val_ej_rj(t->slug_ej, t->slug_rj, t->slug);
            if (STR_EQ(prop, "url")) return val_ej_rj(t->url_ej, t->url_rj, t->url);
            return val_undef();
        }
        case VK_TOC_ITEM: {
            const TocItem *h = (const TocItem *)obj.ptr;
            if (STR_EQ(prop, "level")) return val_u32(h->level);
            if (STR_EQ(prop, "id")) return val_ej_rj(h->id, h->id, h->id); // slug is pure [a-z0-9-]
            if (STR_EQ(prop, "html")) return val_md(h->html);
            return val_undef();
        }
        case VK_PAGER: {
            const PagerObj *pg = (const PagerObj *)obj.ptr;
            if (STR_EQ(prop, "number")) return val_u32(pg->number);
            if (STR_EQ(prop, "total")) return val_u32(pg->total);
            if (STR_EQ(prop, "url")) return val_ej_rj(pg->url_ej, pg->url_rj, pg->url);
            if (STR_EQ(prop, "newer_url")) return val_ej_rj(pg->newer_url_ej, pg->newer_url_rj, pg->newer_url);
            if (STR_EQ(prop, "older_url")) return val_ej_rj(pg->older_url_ej, pg->older_url_rj, pg->older_url);
            return val_undef();
        }
        case VK_LOOP: {
            const LoopObj *lp = (const LoopObj *)obj.ptr;
            if (STR_EQ(prop, "index")) return val_u32(lp->index);
            if (STR_EQ(prop, "first")) return val_bool(lp->first);
            if (STR_EQ(prop, "last")) return val_bool(lp->last);
            if (STR_EQ(prop, "length")) return val_u32(lp->length);
            return val_undef();
        }
        default:
            return val_undef();
    }
}

typedef struct {
    Str name;
    TVal val;
    LoopObj loop;
    int has_loop;
} ScopeFrame;

typedef struct {
    const SiteObj *site;
    const TagObj *all_tags;
    uint32_t num_all_tags;
    PageObj *const *recent;
    uint32_t num_recent;
    // Dynamic root vars (NULL if absent for this layout)
    const PageObj *page;
    PageObj *const *posts;
    uint32_t num_posts;
    int has_posts;
    const PagerObj *pager;
    const TagObj *tag;
    // Active loop stack
    ScopeFrame frames[32];
    int depth;
    // Direct JSON mode
    int direct_json_md;
    // Memoized card cache (indexed by memo_id * total_posts + post_idx)
    Str *card_memo;
    uint32_t total_posts;
    Arena *arena;
} RenderCtx;

static TVal lookup_path(const RenderCtx *ctx, const TPath *path) {
    Str root = path->segs[0];
    TVal v = val_undef();
    int found = 0;
    // Walk loop scopes innermost-first (loop takes precedence over element name if both are "loop")
    for (int d = ctx->depth - 1; d >= 0; d--) {
        if (ctx->frames[d].has_loop && STR_EQ(root, "loop")) {
            v.kind = VK_LOOP; v.ptr = &ctx->frames[d].loop; found = 1; break;
        }
        if (str_eq(ctx->frames[d].name, root)) {
            v = ctx->frames[d].val; found = 1; break;
        }
    }
    if (!found) {
        if (STR_EQ(root, "site")) { v.kind = VK_SITE; v.ptr = ctx->site; }
        else if (STR_EQ(root, "tags")) { v.kind = VK_ARR_TAGS; v.ptr = ctx->all_tags; v.uval = ctx->num_all_tags; }
        else if (STR_EQ(root, "recent")) { v.kind = VK_ARR_CARDS; v.ptr = ctx->recent; v.uval = ctx->num_recent; }
        else if (STR_EQ(root, "page") && ctx->page) { v.kind = VK_PAGE; v.ptr = ctx->page; }
        else if (STR_EQ(root, "posts") && ctx->has_posts) { v.kind = VK_ARR_CARDS; v.ptr = ctx->posts; v.uval = ctx->num_posts; }
        else if (STR_EQ(root, "pager") && ctx->pager) { v.kind = VK_PAGER; v.ptr = ctx->pager; }
        else if (STR_EQ(root, "tag") && ctx->tag) { v.kind = VK_TAG_FULL; v.ptr = ctx->tag; }
        else return val_undef();
    }
    for (uint8_t k = 1; k < path->nsegs; k++) {
        v = val_prop(v, path->segs[k]);
        if (v.kind == VK_UNDEF) return v;
    }
    return v;
}

// Emit TVal directly in JSON-escaped form! (escaped=1 for {{ }}, escaped=0 for {{{ }}})
static void emit_val_json(Buf *out, TVal v, int escaped, int direct_json_md) {
    switch (v.kind) {
        case VK_STR_EJ_RJ:
            buf_put_str(out, escaped ? v.s1 : v.s2);
            break;
        case VK_STR_MD:
            if (direct_json_md && !escaped) {
                buf_put_str(out, v.s1); // Already JSON-escaped HTML! One memcpy!
            } else if (escaped) {
                buf_append_html_json_escaped(out, v.s1.p, v.s1.len);
            } else {
                buf_append_json_escaped(out, v.s1.p, v.s1.len);
            }
            break;
        case VK_STR_RAW:
            if (escaped) buf_append_html_json_escaped(out, v.s1.p, v.s1.len);
            else buf_append_json_escaped(out, v.s1.p, v.s1.len);
            break;
        case VK_INT:
            buf_put_u32(out, v.uval);
            break;
        case VK_BOOL:
            if (v.bval) buf_lit(out, "true"); else buf_lit(out, "false");
            break;
        case VK_ARR_TAGS:
        case VK_ARR_TAG_REFS:
        case VK_ARR_CARDS:
        case VK_ARR_TOC:
            // Array of objects: each object's textOf is "", joined by ", " -> (len-1) copies of ", "
            for (uint32_t k = 1; k < v.uval; k++) buf_lit(out, ", ");
            break;
        default:
            break;
    }
}

static inline int fast_op_truthy(uint8_t op, const RenderCtx *ctx) {
    switch (op) {
        case FOP_PAGE_DATE:     return ctx->page->date.len != 0;
        case FOP_PAGE_SUMMARY_EJ: return ctx->page->summary.len != 0;
        case FOP_PAGE_TAGS:     return ctx->page->num_tag_refs != 0;
        case FOP_PAGE_TOC:      return ctx->page->num_toc != 0;
        case FOP_PAGE_NEWER:    return ctx->page->newer != NULL;
        case FOP_PAGE_OLDER:    return ctx->page->older != NULL;
        case FOP_TAG_POSTS:     return ctx->tag->num_posts != 0;
        case FOP_PAGER_NEWER_EJ: return ctx->pager->newer_url.len != 0;
        case FOP_PAGER_OLDER_EJ: return ctx->pager->older_url.len != 0;
        case FOP_L0_SUMMARY_EJ: return ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->summary.len != 0;
        case FOP_L0_TAGS:       return ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->num_tag_refs != 0;
        case FOP_L0_URL_EJ: {
            const TVal *v = &ctx->frames[ctx->depth - 1].val;
            if (v->kind == VK_TAG_REF) return ((const TagRef *)v->ptr)->url.len != 0;
            if (v->kind == VK_CARD) return ((const PageObj *)v->ptr)->url.len != 0;
            return ((const TagObj *)v->ptr)->url.len != 0;
        }
        case FOP_LOOP_INDEX:    return ctx->frames[ctx->depth - 1].loop.index != 0;
        case FOP_LOOP_FIRST:    return ctx->frames[ctx->depth - 1].loop.first != 0;
        case FOP_LOOP_LAST:     return ctx->frames[ctx->depth - 1].loop.last != 0;
        case FOP_LOOP_LENGTH:   return ctx->frames[ctx->depth - 1].loop.length != 0;
        default: return -1;
    }
}

static inline int fast_op_emit(Buf *out, uint8_t op, const RenderCtx *ctx) {
    switch (op) {
        case FOP_PAGE_TITLE_EJ:    buf_put_str(out, ctx->page->title_ej); return 1;
        case FOP_PAGE_URL_EJ:      buf_put_str(out, ctx->page->url_ej); return 1;
        case FOP_PAGE_DATE:        buf_put_str(out, ctx->page->date_ej); return 1;
        case FOP_PAGE_DATE_LONG:   buf_put_str(out, ctx->page->date_long_ej); return 1;
        case FOP_PAGE_SUMMARY_EJ:  buf_put_str(out, ctx->page->summary_ej); return 1;
        case FOP_PAGE_EXCERPT:     buf_put_str(out, ctx->page->excerpt); return 1;
        case FOP_PAGE_CONTENT:     buf_put_str(out, ctx->page->html); return 1;
        case FOP_PAGE_RMIN:        buf_put_u32(out, ctx->page->reading_minutes); return 1;
        case FOP_PAGE_SLUG:        buf_put_str(out, ctx->page->slug_ej); return 1;
        case FOP_PAGE_NEWER_URL:   if (ctx->page->newer) buf_put_str(out, ctx->page->newer->url_ej); return 1;
        case FOP_PAGE_NEWER_TITLE: if (ctx->page->newer) buf_put_str(out, ctx->page->newer->title_ej); return 1;
        case FOP_PAGE_OLDER_URL:   if (ctx->page->older) buf_put_str(out, ctx->page->older->url_ej); return 1;
        case FOP_PAGE_OLDER_TITLE: if (ctx->page->older) buf_put_str(out, ctx->page->older->title_ej); return 1;
        case FOP_TAG_NAME_EJ:      buf_put_str(out, ctx->tag->name_ej); return 1;
        case FOP_TAG_SLUG:         buf_put_str(out, ctx->tag->slug_ej); return 1;
        case FOP_TAG_URL_EJ:       buf_put_str(out, ctx->tag->url_ej); return 1;
        case FOP_TAG_COUNT:        buf_put_u32(out, ctx->tag->num_posts); return 1;
        case FOP_PAGER_NUM:        buf_put_u32(out, ctx->pager->number); return 1;
        case FOP_PAGER_TOTAL:      buf_put_u32(out, ctx->pager->total); return 1;
        case FOP_PAGER_URL_EJ:     buf_put_str(out, ctx->pager->url_ej); return 1;
        case FOP_PAGER_NEWER_EJ:   buf_put_str(out, ctx->pager->newer_url_ej); return 1;
        case FOP_PAGER_OLDER_EJ:   buf_put_str(out, ctx->pager->older_url_ej); return 1;
        case FOP_L0_TITLE_EJ:      buf_put_str(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->title_ej); return 1;
        case FOP_L0_URL_EJ: {
            const TVal *v = &ctx->frames[ctx->depth - 1].val;
            if (v->kind == VK_TAG_REF) buf_put_str(out, ((const TagRef *)v->ptr)->url_ej);
            else if (v->kind == VK_CARD) buf_put_str(out, ((const PageObj *)v->ptr)->url_ej);
            else buf_put_str(out, ((const TagObj *)v->ptr)->url_ej);
            return 1;
        }
        case FOP_L0_DATE:          buf_put_str(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->date_ej); return 1;
        case FOP_L0_DATE_LONG:     buf_put_str(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->date_long_ej); return 1;
        case FOP_L0_SUMMARY_EJ:    buf_put_str(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->summary_ej); return 1;
        case FOP_L0_EXCERPT:       buf_put_str(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->excerpt); return 1;
        case FOP_L0_RMIN:          buf_put_u32(out, ((const PageObj *)ctx->frames[ctx->depth - 1].val.ptr)->reading_minutes); return 1;
        case FOP_L0_NAME_EJ: {
            const TVal *v = &ctx->frames[ctx->depth - 1].val;
            if (v->kind == VK_TAG_REF) buf_put_str(out, ((const TagRef *)v->ptr)->name_ej);
            else buf_put_str(out, ((const TagObj *)v->ptr)->name_ej);
            return 1;
        }
        case FOP_L0_SLUG: {
            const TVal *v = &ctx->frames[ctx->depth - 1].val;
            if (v->kind == VK_TAG_REF) buf_put_str(out, ((const TagRef *)v->ptr)->slug_ej);
            else buf_put_str(out, ((const TagObj *)v->ptr)->slug_ej);
            return 1;
        }
        case FOP_L0_LEVEL:         buf_put_u32(out, ((const TocItem *)ctx->frames[ctx->depth - 1].val.ptr)->level); return 1;
        case FOP_L0_ID:            buf_put_str(out, ((const TocItem *)ctx->frames[ctx->depth - 1].val.ptr)->id); return 1;
        case FOP_L0_HTML:          buf_put_str(out, ((const TocItem *)ctx->frames[ctx->depth - 1].val.ptr)->html); return 1;
        case FOP_LOOP_INDEX:       buf_put_u32(out, ctx->frames[ctx->depth - 1].loop.index); return 1;
        default: return 0;
    }
}

static void render_compiled_nodes(Buf *out, const TNodeList *list, RenderCtx *ctx) {
    const uint32_t n = list->len;
    const TNode *items = list->items;
    const int dj = ctx->direct_json_md;

    for (uint32_t i = 0; i < n; i++) {
        const TNode *nd = &items[i];
        switch (nd->kind) {
            case NK_LIT_JSON:
                buf_put_str(out, nd->text);
                break;
            case NK_VAR:
                if (nd->fast_op && fast_op_emit(out, nd->fast_op, ctx)) break;
                emit_val_json(out, lookup_path(ctx, &nd->path), 1, dj);
                break;
            case NK_RAW:
                if (nd->fast_op && fast_op_emit(out, nd->fast_op, ctx)) break;
                emit_val_json(out, lookup_path(ctx, &nd->path), 0, dj);
                break;
            case NK_IF: {
                int t = nd->fast_op ? fast_op_truthy(nd->fast_op, ctx) : -1;
                if (t < 0) t = val_truthy(lookup_path(ctx, &nd->path));
                if (t != (int)nd->neg) render_compiled_nodes(out, &nd->body, ctx);
                else if (nd->has_else) render_compiled_nodes(out, &nd->els, ctx);
                break;
            }
            case NK_MEMO_FOR: {
                TVal arr = lookup_path(ctx, &nd->path);
                if (arr.kind != VK_ARR_CARDS || arr.uval == 0) break;
                PageObj *const *cards = (PageObj *const *)arr.ptr;
                Str *memo_base = ctx->card_memo + (size_t)nd->memo_id * ctx->total_posts;
                int d = ctx->depth++;
                ctx->frames[d].name = nd->text;
                ctx->frames[d].has_loop = 1;
                ctx->frames[d].loop.length = arr.uval;
                for (uint32_t k = 0; k < arr.uval; k++) {
                    PageObj *po = cards[k];
                    Str cached = memo_base[po->post_idx];
                    if (cached.p != NULL) {
                        buf_put_str(out, cached);
                    } else {
                        ctx->frames[d].val.kind = VK_CARD;
                        ctx->frames[d].val.ptr = po;
                        ctx->frames[d].loop.index = k + 1;
                        ctx->frames[d].loop.first = (k == 0);
                        ctx->frames[d].loop.last = (k + 1 == arr.uval);
                        size_t st = out->len;
                        render_compiled_nodes(out, &nd->body, ctx);
                        size_t clen = out->len - st;
                        char *copy = arena_alloc_str(ctx->arena, clen);
                        fast_copy(copy, out->data + st, clen);
                        memo_base[po->post_idx] = (Str){ copy, (uint32_t)clen };
                    }
                }
                ctx->depth--;
                break;
            }
            case NK_FOR: {
                TVal arr = lookup_path(ctx, &nd->path);
                uint32_t alen = arr.uval;
                if (alen == 0) break;
                if (arr.kind != VK_ARR_TAGS && arr.kind != VK_ARR_TAG_REFS && arr.kind != VK_ARR_CARDS && arr.kind != VK_ARR_TOC) break;
                int d = ctx->depth++;
                ctx->frames[d].name = nd->text;
                ctx->frames[d].has_loop = 1;
                ctx->frames[d].loop.length = alen;
                for (uint32_t k = 0; k < alen; k++) {
                    TVal elem = {0};
                    if (arr.kind == VK_ARR_TAGS) {
                        elem.kind = VK_TAG_BRIEF;
                        elem.ptr = &((const TagObj *)arr.ptr)[k];
                    } else if (arr.kind == VK_ARR_TAG_REFS) {
                        elem.kind = VK_TAG_REF;
                        elem.ptr = &((const TagRef *)arr.ptr)[k];
                    } else if (arr.kind == VK_ARR_CARDS) {
                        elem.kind = VK_CARD;
                        elem.ptr = ((PageObj *const *)arr.ptr)[k];
                    } else {
                        elem.kind = VK_TOC_ITEM;
                        elem.ptr = &((const TocItem *)arr.ptr)[k];
                    }
                    ctx->frames[d].val = elem;
                    ctx->frames[d].loop.index = k + 1;
                    ctx->frames[d].loop.first = (k == 0);
                    ctx->frames[d].loop.last = (k + 1 == alen);
                    render_compiled_nodes(out, &nd->body, ctx);
                }
                ctx->depth--;
                break;
            }
            default:
                break;
        }
    }
}

// ---- Template Optimizer: Include Inlining + Site Constant Folding + Coalescing + Card Memo Detection ----
// Layout modes for partial evaluation:
enum {
    LM_PAGE = 0,  // page is present (truthy object); posts, pager, tag are absent
    LM_INDEX = 1, // posts, pager present; page, tag absent
    LM_TAG = 2,   // tag present (truthy object); page, posts, pager absent
    LM_TAGS = 3,  // page, posts, pager, tag ALL absent!
};

static int is_const_root(Str root, int lmode, const Str *bound, int nbound) {
    for (int i = nbound - 1; i >= 0; i--) {
        if (str_eq(bound[i], root)) return 0; // loop-bound variable is dynamic wrt outer scope
    }
    if (STR_EQ(root, "site") || STR_EQ(root, "tags") || STR_EQ(root, "recent")) return 1;
    // Any variable guaranteed undefined for this layout is ALSO constant!
    if (STR_EQ(root, "page")) return lmode != LM_PAGE;
    if (STR_EQ(root, "posts") || STR_EQ(root, "pager")) return lmode != LM_INDEX;
    if (STR_EQ(root, "tag")) return lmode != LM_TAG;
    return 1; // any unknown root is always undefined!
}

// Check if a subtree depends ONLY on site-constants (plus loop vars bound INSIDE it)
static int subtree_is_site_const(const TNodeList *nl, int lmode, const Str *outer_bound, int n_outer) {
    for (uint32_t i = 0; i < nl->len; i++) {
        const TNode *nd = &nl->items[i];
        switch (nd->kind) {
            case NK_LIT_JSON: case NK_TEXT: break;
            case NK_VAR: case NK_RAW:
                if (!is_const_root(nd->path.segs[0], lmode, outer_bound, n_outer)) return 0;
                break;
            case NK_IF:
                if (!is_const_root(nd->path.segs[0], lmode, outer_bound, n_outer)) return 0;
                if (!subtree_is_site_const(&nd->body, lmode, outer_bound, n_outer)) return 0;
                if (nd->has_else && !subtree_is_site_const(&nd->els, lmode, outer_bound, n_outer)) return 0;
                break;
            case NK_FOR: {
                if (!is_const_root(nd->path.segs[0], lmode, outer_bound, n_outer)) return 0;
                // Inner loop binds nd->text and "loop", which are local to this loop -> do NOT treat as outer dynamic!
                // Simply check that inner nodes don't reference any outer_bound or dynamic layout roots!
                if (!subtree_is_site_const(&nd->body, lmode, outer_bound, n_outer)) return 0;
                break;
            }
            default: return 0;
        }
    }
    return 1;
}

// Check if a post-card FOR body depends ONLY on (item_var + site-consts) and NOT on "loop" or outer dynamic vars
static int card_body_is_pure(const TNodeList *nl, Str item_var, const Str *inner_bound, int n_inner) {
    for (uint32_t i = 0; i < nl->len; i++) {
        const TNode *nd = &nl->items[i];
        switch (nd->kind) {
            case NK_LIT_JSON: case NK_TEXT: break;
            case NK_VAR: case NK_RAW: {
                Str r = nd->path.segs[0];
                if (STR_EQ(r, "site") || STR_EQ(r, "tags") || STR_EQ(r, "recent") || str_eq(r, item_var)) break;
                int ok = 0;
                for (int k = 0; k < n_inner; k++) if (str_eq(inner_bound[k], r)) { ok = 1; break; }
                if (!ok) return 0; // references loop, page, tag, pager, posts!
                break;
            }
            case NK_IF: {
                Str r = nd->path.segs[0];
                int ok = STR_EQ(r, "site") || STR_EQ(r, "tags") || STR_EQ(r, "recent") || str_eq(r, item_var);
                for (int k = 0; !ok && k < n_inner; k++) if (str_eq(inner_bound[k], r)) ok = 1;
                if (!ok) return 0;
                if (!card_body_is_pure(&nd->body, item_var, inner_bound, n_inner)) return 0;
                if (nd->has_else && !card_body_is_pure(&nd->els, item_var, inner_bound, n_inner)) return 0;
                break;
            }
            case NK_FOR: {
                Str r = nd->path.segs[0];
                int ok = STR_EQ(r, "site") || STR_EQ(r, "tags") || STR_EQ(r, "recent") || str_eq(r, item_var);
                for (int k = 0; !ok && k < n_inner; k++) if (str_eq(inner_bound[k], r)) ok = 1;
                if (!ok) return 0;
                Str nb[32];
                for (int k = 0; k < n_inner && k < 30; k++) nb[k] = inner_bound[k];
                int nn = n_inner < 30 ? n_inner : 30;
                nb[nn++] = nd->text;
                nb[nn++] = STR_LIT("loop");
                if (!card_body_is_pure(&nd->body, item_var, nb, nn)) return 0;
                break;
            }
            default: return 0;
        }
    }
    return 1;
}

static void inline_and_prepare(
    Arena *a,
    const TNodeList *src,
    const StrMap *tpl_map,
    const ParsedTemplate *tpls,
    int inc_depth,
    TNodeList *out
) {
    for (uint32_t i = 0; i < src->len; i++) {
        const TNode *nd = &src->items[i];
        if (nd->kind == NK_INCLUDE) {
            uint32_t tidx;
            if (inc_depth < TPL_INCLUDE_DEPTH && strmap_get(tpl_map, nd->text, &tidx)) {
                inline_and_prepare(a, &tpls[tidx].nodes, tpl_map, tpls, inc_depth + 1, out);
            }
            continue;
        }
        if (nd->kind == NK_TEXT) {
            TNode tn = {0};
            tn.kind = NK_LIT_JSON;
            tn.text = arena_json_escape(a, nd->text);
            nlist_push(a, out, tn);
            continue;
        }
        TNode cp = *nd;
        cp.body = (TNodeList){0};
        cp.els = (TNodeList){0};
        if (nd->kind == NK_IF) {
            inline_and_prepare(a, &nd->body, tpl_map, tpls, inc_depth, &cp.body);
            if (nd->has_else) inline_and_prepare(a, &nd->els, tpl_map, tpls, inc_depth, &cp.els);
        } else if (nd->kind == NK_FOR) {
            inline_and_prepare(a, &nd->body, tpl_map, tpls, inc_depth, &cp.body);
        }
        nlist_push(a, out, cp);
    }
}

// Recursive exact AST equality for cross-layout memo sharing
static int tnodelist_equal(const TNodeList *a, const TNodeList *b) {
    if (a->len != b->len) return 0;
    for (uint32_t i = 0; i < a->len; i++) {
        const TNode *x = &a->items[i], *y = &b->items[i];
        if (x->kind != y->kind || x->neg != y->neg || x->fast_op != y->fast_op || x->has_else != y->has_else) return 0;
        if (!str_eq(x->text, y->text)) return 0;
        if (x->path.nsegs != y->path.nsegs) return 0;
        for (uint8_t s = 0; s < x->path.nsegs; s++) if (!str_eq(x->path.segs[s], y->path.segs[s])) return 0;
        if (!tnodelist_equal(&x->body, &y->body)) return 0;
        if (x->has_else && !tnodelist_equal(&x->els, &y->els)) return 0;
    }
    return 1;
}

// Resolve static fast_op for VAR / RAW / IF nodes given active loop bindings & item kind
static uint8_t resolve_fast_op(const TNode *nd, int lmode, int direct_json_md, const Str *bound, int nbound, int top_kind) {
    Str r = nd->path.segs[0];
    int is_var = (nd->kind == NK_VAR);
    int is_raw = (nd->kind == NK_RAW);
    int is_if  = (nd->kind == NK_IF);

    // Check if shadowed by any bound loop variable
    int bound_idx = -1;
    for (int k = nbound - 1; k >= 0; k--) {
        if (str_eq(bound[k], r)) { bound_idx = k; break; }
    }
    if (bound_idx >= 0) {
        // Only specialize innermost loop (bound[nbound-2] = item var, bound[nbound-1] = "loop")
        if (bound_idx == nbound - 1 && STR_EQ(r, "loop") && nd->path.nsegs == 2) {
            Str p1 = nd->path.segs[1];
            if (STR_EQ(p1, "index")) return FOP_LOOP_INDEX;
            if (is_if) {
                if (STR_EQ(p1, "first")) return FOP_LOOP_FIRST;
                if (STR_EQ(p1, "last")) return FOP_LOOP_LAST;
                if (STR_EQ(p1, "length")) return FOP_LOOP_LENGTH;
            }
            return FOP_NONE;
        }
        if (bound_idx == nbound - 2 && nd->path.nsegs == 2 && !STR_EQ(r, "loop")) {
            Str p1 = nd->path.segs[1];
            if (top_kind == VK_CARD) {
                if (is_var) {
                    if (STR_EQ(p1, "title")) return FOP_L0_TITLE_EJ;
                    if (STR_EQ(p1, "url")) return FOP_L0_URL_EJ;
                    if (STR_EQ(p1, "date")) return FOP_L0_DATE;
                    if (STR_EQ(p1, "date_long")) return FOP_L0_DATE_LONG;
                    if (STR_EQ(p1, "summary")) return FOP_L0_SUMMARY_EJ;
                    if (STR_EQ(p1, "reading_minutes")) return FOP_L0_RMIN;
                } else if (is_raw && direct_json_md && STR_EQ(p1, "excerpt")) return FOP_L0_EXCERPT;
                else if (is_if) {
                    if (STR_EQ(p1, "summary")) return FOP_L0_SUMMARY_EJ;
                    if (STR_EQ(p1, "tags")) return FOP_L0_TAGS;
                }
            } else if (top_kind == VK_TAG_REF || top_kind == VK_TAG_BRIEF) {
                if (is_var) {
                    if (STR_EQ(p1, "name")) return FOP_L0_NAME_EJ;
                    if (STR_EQ(p1, "slug")) return FOP_L0_SLUG;
                    if (STR_EQ(p1, "url")) return FOP_L0_URL_EJ;
                } else if (is_if && STR_EQ(p1, "url")) return FOP_L0_URL_EJ;
            } else if (top_kind == VK_TOC_ITEM) {
                if (is_var) {
                    if (STR_EQ(p1, "level")) return FOP_L0_LEVEL;
                    if (STR_EQ(p1, "id")) return FOP_L0_ID;
                } else if (is_raw && direct_json_md && STR_EQ(p1, "html")) return FOP_L0_HTML;
            }
        }
        return FOP_NONE;
    }

    // Root variable (unshadowed!)
    if (lmode == LM_PAGE && STR_EQ(r, "page")) {
        if (nd->path.nsegs == 2) {
            Str p1 = nd->path.segs[1];
            if (is_var) {
                if (STR_EQ(p1, "title")) return FOP_PAGE_TITLE_EJ;
                if (STR_EQ(p1, "url")) return FOP_PAGE_URL_EJ;
                if (STR_EQ(p1, "date")) return FOP_PAGE_DATE;
                if (STR_EQ(p1, "date_long")) return FOP_PAGE_DATE_LONG;
                if (STR_EQ(p1, "summary")) return FOP_PAGE_SUMMARY_EJ;
                if (STR_EQ(p1, "reading_minutes")) return FOP_PAGE_RMIN;
                if (STR_EQ(p1, "slug")) return FOP_PAGE_SLUG;
            } else if (is_raw && direct_json_md) {
                if (STR_EQ(p1, "content")) return FOP_PAGE_CONTENT;
                if (STR_EQ(p1, "excerpt")) return FOP_PAGE_EXCERPT;
            } else if (is_if) {
                if (STR_EQ(p1, "date")) return FOP_PAGE_DATE;
                if (STR_EQ(p1, "summary")) return FOP_PAGE_SUMMARY_EJ;
                if (STR_EQ(p1, "tags")) return FOP_PAGE_TAGS;
                if (STR_EQ(p1, "toc")) return FOP_PAGE_TOC;
                if (STR_EQ(p1, "newer")) return FOP_PAGE_NEWER;
                if (STR_EQ(p1, "older")) return FOP_PAGE_OLDER;
            }
        } else if (nd->path.nsegs == 3 && is_var) {
            Str p1 = nd->path.segs[1], p2 = nd->path.segs[2];
            if (STR_EQ(p1, "newer")) {
                if (STR_EQ(p2, "url")) return FOP_PAGE_NEWER_URL;
                if (STR_EQ(p2, "title")) return FOP_PAGE_NEWER_TITLE;
            } else if (STR_EQ(p1, "older")) {
                if (STR_EQ(p2, "url")) return FOP_PAGE_OLDER_URL;
                if (STR_EQ(p2, "title")) return FOP_PAGE_OLDER_TITLE;
            }
        }
    } else if (lmode == LM_TAG && STR_EQ(r, "tag") && nd->path.nsegs == 2) {
        Str p1 = nd->path.segs[1];
        if (is_var) {
            if (STR_EQ(p1, "name")) return FOP_TAG_NAME_EJ;
            if (STR_EQ(p1, "slug")) return FOP_TAG_SLUG;
            if (STR_EQ(p1, "url")) return FOP_TAG_URL_EJ;
            if (STR_EQ(p1, "count")) return FOP_TAG_COUNT;
        } else if (is_if && STR_EQ(p1, "posts")) return FOP_TAG_POSTS;
    } else if (lmode == LM_INDEX && STR_EQ(r, "pager") && nd->path.nsegs == 2) {
        Str p1 = nd->path.segs[1];
        if (is_var) {
            if (STR_EQ(p1, "number")) return FOP_PAGER_NUM;
            if (STR_EQ(p1, "total")) return FOP_PAGER_TOTAL;
            if (STR_EQ(p1, "url")) return FOP_PAGER_URL_EJ;
            if (STR_EQ(p1, "newer_url")) return FOP_PAGER_NEWER_EJ;
            if (STR_EQ(p1, "older_url")) return FOP_PAGER_OLDER_EJ;
        } else if (is_if) {
            if (STR_EQ(p1, "newer_url")) return FOP_PAGER_NEWER_EJ;
            if (STR_EQ(p1, "older_url")) return FOP_PAGER_OLDER_EJ;
        }
    }
    return FOP_NONE;
}

static int infer_for_elem_kind(const TPath *path, int lmode, const Str *bound, int nbound, int top_kind) {
    Str r = path->segs[0];
    int bound_idx = -1;
    for (int k = nbound - 1; k >= 0; k--) if (str_eq(bound[k], r)) { bound_idx = k; break; }
    if (bound_idx >= 0) {
        if (bound_idx == nbound - 2 && top_kind == VK_CARD && path->nsegs == 2 && STR_EQ(path->segs[1], "tags"))
            return VK_TAG_REF;
        return 0;
    }
    if (path->nsegs == 1) {
        if (STR_EQ(r, "tags")) return VK_TAG_BRIEF;
        if (STR_EQ(r, "recent")) return VK_CARD;
        if (lmode == LM_INDEX && STR_EQ(r, "posts")) return VK_CARD;
    } else if (path->nsegs == 2) {
        if (lmode == LM_PAGE && STR_EQ(r, "page")) {
            if (STR_EQ(path->segs[1], "toc")) return VK_TOC_ITEM;
            if (STR_EQ(path->segs[1], "tags")) return VK_TAG_REF;
        }
        if (lmode == LM_TAG && STR_EQ(r, "tag") && STR_EQ(path->segs[1], "posts")) return VK_CARD;
    }
    return 0;
}

// Partial-evaluate constant branches/vars/loops, specialize fast_op, and merge adjacent NK_LIT_JSON chunks!
static void fold_nodes_ex(
    Arena *a,
    const TNodeList *src,
    int lmode,
    RenderCtx *cctx,
    Buf *fold_buf,
    const Str *bound,
    int nbound,
    int top_kind,
    uint16_t *next_memo_id,
    TNodeList *out
) {
    TNodeList raw_out = {0};
    for (uint32_t i = 0; i < src->len; i++) {
        const TNode *nd = &src->items[i];
        if (nd->kind == NK_LIT_JSON) {
            if (nd->text.len > 0) nlist_push(a, &raw_out, *nd);
            continue;
        }
        if (nd->kind == NK_VAR || nd->kind == NK_RAW) {
            if (is_const_root(nd->path.segs[0], lmode, bound, nbound)) {
                fold_buf->len = 0;
                emit_val_json(fold_buf, lookup_path(cctx, &nd->path), nd->kind == NK_VAR, cctx->direct_json_md);
                if (fold_buf->len > 0) {
                    char *copy = arena_alloc_str(a, fold_buf->len);
                    fast_copy(copy, fold_buf->data, fold_buf->len);
                    TNode ln = {0}; ln.kind = NK_LIT_JSON; ln.text = (Str){ copy, (uint32_t)fold_buf->len };
                    nlist_push(a, &raw_out, ln);
                }
                continue;
            }
            TNode cp = *nd;
            cp.fast_op = resolve_fast_op(nd, lmode, cctx->direct_json_md, bound, nbound, top_kind);
            nlist_push(a, &raw_out, cp);
            continue;
        }
        if (nd->kind == NK_IF) {
            Str r = nd->path.segs[0];
            int known = -1;
            if (is_const_root(r, lmode, bound, nbound)) {
                known = val_truthy(lookup_path(cctx, &nd->path));
            } else if (nd->path.nsegs == 1) {
                int is_bound = 0;
                for (int k = 0; k < nbound; k++) if (str_eq(bound[k], r)) { is_bound = 1; break; }
                if (!is_bound) {
                    if (lmode == LM_PAGE && STR_EQ(r, "page")) known = 1;
                    else if (lmode == LM_TAG && STR_EQ(r, "tag")) known = 1;
                    else if (lmode == LM_INDEX && STR_EQ(r, "pager")) known = 1;
                }
            }
            if (known >= 0) {
                const TNodeList *branch = (known != (int)nd->neg) ? &nd->body : (nd->has_else ? &nd->els : NULL);
                if (branch) {
                    TNodeList folded = {0};
                    fold_nodes_ex(a, branch, lmode, cctx, fold_buf, bound, nbound, top_kind, next_memo_id, &folded);
                    for (uint32_t k = 0; k < folded.len; k++) nlist_push(a, &raw_out, folded.items[k]);
                }
                continue;
            }
            TNode cp = *nd;
            cp.fast_op = resolve_fast_op(nd, lmode, cctx->direct_json_md, bound, nbound, top_kind);
            cp.body = (TNodeList){0};
            cp.els = (TNodeList){0};
            fold_nodes_ex(a, &nd->body, lmode, cctx, fold_buf, bound, nbound, top_kind, next_memo_id, &cp.body);
            if (nd->has_else) fold_nodes_ex(a, &nd->els, lmode, cctx, fold_buf, bound, nbound, top_kind, next_memo_id, &cp.els);
            nlist_push(a, &raw_out, cp);
            continue;
        }
        if (nd->kind == NK_FOR) {
            if (nbound == 0 && subtree_is_site_const(&(TNodeList){ (TNode *)nd, 1, 1 }, lmode, bound, nbound)) {
                fold_buf->len = 0;
                TNodeList one = { (TNode *)nd, 1, 1 };
                render_compiled_nodes(fold_buf, &one, cctx);
                if (fold_buf->len > 0) {
                    char *copy = arena_alloc_str(a, fold_buf->len);
                    fast_copy(copy, fold_buf->data, fold_buf->len);
                    TNode ln = {0}; ln.kind = NK_LIT_JSON; ln.text = (Str){ copy, (uint32_t)fold_buf->len };
                    nlist_push(a, &raw_out, ln);
                }
                continue;
            }
            TNode cp = *nd;
            cp.body = (TNodeList){0};
            Str nb[32];
            for (int k = 0; k < nbound && k < 30; k++) nb[k] = bound[k];
            int nn = nbound < 30 ? nbound : 30;
            int elem_kind = infer_for_elem_kind(&nd->path, lmode, bound, nbound, top_kind);
            nb[nn++] = nd->text;
            nb[nn++] = STR_LIT("loop");
            fold_nodes_ex(a, &nd->body, lmode, cctx, fold_buf, nb, nn, elem_kind, next_memo_id, &cp.body);
            int is_posts_loop = (nbound == 0) && (
                (lmode == LM_INDEX && nd->path.nsegs == 1 && STR_EQ(nd->path.segs[0], "posts")) ||
                (lmode == LM_TAG && nd->path.nsegs == 2 && STR_EQ(nd->path.segs[0], "tag") && STR_EQ(nd->path.segs[1], "posts"))
            );
            if (is_posts_loop && card_body_is_pure(&cp.body, nd->text, NULL, 0) && *next_memo_id < 8) {
                cp.kind = NK_MEMO_FOR;
                cp.memo_id = (*next_memo_id)++;
            }
            nlist_push(a, &raw_out, cp);
            continue;
        }
    }

    uint32_t i = 0;
    while (i < raw_out.len) {
        if (raw_out.items[i].kind != NK_LIT_JSON) {
            nlist_push(a, out, raw_out.items[i++]);
            continue;
        }
        uint32_t j = i + 1;
        size_t total = raw_out.items[i].text.len;
        while (j < raw_out.len && raw_out.items[j].kind == NK_LIT_JSON) {
            total += raw_out.items[j].text.len;
            j++;
        }
        if (j == i + 1) {
            nlist_push(a, out, raw_out.items[i]);
        } else {
            char *merged = arena_alloc_str(a, total);
            size_t pos = 0;
            for (uint32_t k = i; k < j; k++) {
                fast_copy(merged + pos, raw_out.items[k].text.p, raw_out.items[k].text.len);
                pos += raw_out.items[k].text.len;
            }
            TNode ln = {0}; ln.kind = NK_LIT_JSON; ln.text = (Str){ merged, (uint32_t)total };
            nlist_push(a, out, ln);
        }
        i = j;
    }
}

static inline void fold_nodes(
    Arena *a, const TNodeList *src, int lmode, RenderCtx *cctx, Buf *fold_buf,
    const Str *bound, int nbound, uint16_t *next_memo_id, TNodeList *out
) {
    fold_nodes_ex(a, src, lmode, cctx, fold_buf, bound, nbound, 0, next_memo_id, out);
}
