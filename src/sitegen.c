#include "sys_simd.h"
#include "json_parse.h"
#include "text_fm.h"
#include "markdown.h"
#include "template.h"

// ---- Generic iterative/recursive introsort (no libc qsort needed!) ----
static void sort_raw_templates(RawTemplate *a, int n) {
    for (int i = 1; i < n; i++) {
        RawTemplate key = a[i];
        int j = i - 1;
        while (j >= 0 && cmp_code_units(a[j].name, key.name) > 0) {
            a[j + 1] = a[j];
            j--;
        }
        a[j + 1] = key;
    }
}

static void sort_raw_files(RawFile *a, int lo, int hi) {
    while (hi - lo > 16) {
        int mid = lo + ((hi - lo) >> 1);
        // median of 3
        if (cmp_code_units(a[mid].path, a[lo].path) < 0) { RawFile t = a[lo]; a[lo] = a[mid]; a[mid] = t; }
        if (cmp_code_units(a[hi].path, a[lo].path) < 0) { RawFile t = a[lo]; a[lo] = a[hi]; a[hi] = t; }
        if (cmp_code_units(a[hi].path, a[mid].path) < 0) { RawFile t = a[mid]; a[mid] = a[hi]; a[hi] = t; }
        Str piv = a[mid].path;
        int i = lo, j = hi;
        while (i <= j) {
            while (cmp_code_units(a[i].path, piv) < 0) i++;
            while (cmp_code_units(a[j].path, piv) > 0) j--;
            if (i <= j) { RawFile t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) {
            if (lo < j) sort_raw_files(a, lo, j);
            lo = i;
        } else {
            if (i < hi) sort_raw_files(a, i, hi);
            hi = j;
        }
    }
    for (int i = lo + 1; i <= hi; i++) {
        RawFile key = a[i];
        int j = i - 1;
        while (j >= lo && cmp_code_units(a[j].path, key.path) > 0) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

static inline int cmp_posts(const PageObj *x, const PageObj *y) {
    int c = cmp_code_units(y->date, x->date); // newest first
    if (c != 0) return c;
    return cmp_code_units(x->slug, y->slug);  // then slug asc
}

static void sort_posts(PageObj **a, int lo, int hi) {
    while (hi - lo > 16) {
        int mid = lo + ((hi - lo) >> 1);
        if (cmp_posts(a[mid], a[lo]) < 0) { PageObj *t = a[lo]; a[lo] = a[mid]; a[mid] = t; }
        if (cmp_posts(a[hi], a[lo]) < 0) { PageObj *t = a[lo]; a[lo] = a[hi]; a[hi] = t; }
        if (cmp_posts(a[hi], a[mid]) < 0) { PageObj *t = a[mid]; a[mid] = a[hi]; a[hi] = t; }
        PageObj *piv = a[mid];
        int i = lo, j = hi;
        while (i <= j) {
            while (cmp_posts(a[i], piv) < 0) i++;
            while (cmp_posts(a[j], piv) > 0) j--;
            if (i <= j) { PageObj *t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) {
            if (lo < j) sort_posts(a, lo, j);
            lo = i;
        } else {
            if (i < hi) sort_posts(a, i, hi);
            hi = j;
        }
    }
    for (int i = lo + 1; i <= hi; i++) {
        PageObj *key = a[i];
        int j = i - 1;
        while (j >= lo && cmp_posts(a[j], key) > 0) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

static inline int cmp_tags(const TagObj *x, const TagObj *y) {
    if (x->num_posts != y->num_posts) return (y->num_posts > x->num_posts) ? 1 : -1;
    return cmp_code_units(x->slug, y->slug);
}

static void sort_tags(TagObj *a, int n) {
    for (int i = 1; i < n; i++) {
        TagObj key = a[i];
        int j = i - 1;
        while (j >= 0 && cmp_tags(&a[j], &key) > 0) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

static void sort_strs(Str *a, int lo, int hi) {
    while (hi - lo > 16) {
        int mid = lo + ((hi - lo) >> 1);
        if (cmp_code_units(a[mid], a[lo]) < 0) { Str t = a[lo]; a[lo] = a[mid]; a[mid] = t; }
        if (cmp_code_units(a[hi], a[lo]) < 0) { Str t = a[lo]; a[lo] = a[hi]; a[hi] = t; }
        if (cmp_code_units(a[hi], a[mid]) < 0) { Str t = a[mid]; a[mid] = a[hi]; a[hi] = t; }
        Str piv = a[mid];
        int i = lo, j = hi;
        while (i <= j) {
            while (cmp_code_units(a[i], piv) < 0) i++;
            while (cmp_code_units(a[j], piv) > 0) j--;
            if (i <= j) { Str t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (j - lo < hi - i) {
            if (lo < j) sort_strs(a, lo, j);
            lo = i;
        } else {
            if (i < hi) sort_strs(a, i, hi);
            hi = j;
        }
    }
    for (int i = lo + 1; i <= hi; i++) {
        Str key = a[i];
        int j = i - 1;
        while (j >= lo && cmp_code_units(a[j], key) > 0) { a[j + 1] = a[j]; j--; }
        a[j + 1] = key;
    }
}

static inline Str dir_of(Str path) {
    for (int32_t i = (int32_t)path.len - 1; i >= 0; i--) {
        if (path.p[i] == '/') return (Str){ path.p, (uint32_t)i };
    }
    return STR_EMPTY;
}

static inline Str normalize_cr_str(Arena *a, Str s) {
    // Check if any '\r' exists
    uint32_t i = 0;
    while (i < s.len && s.p[i] != '\r') i++;
    if (i == s.len) return s;
    char *dst = arena_alloc_str(a, s.len);
    fast_copy(dst, s.p, i);
    uint32_t out = i;
    while (i < s.len) {
        if (s.p[i] == '\r') {
            dst[out++] = '\n';
            if (i + 1 < s.len && s.p[i + 1] == '\n') i += 2;
            else i++;
        } else {
            dst[out++] = s.p[i++];
        }
    }
    return (Str){ dst, out };
}

// Helper to emit a JSON object key + opening quote:  ,"key":"  (or "key":" if first)
static inline void emit_json_kv_open(Buf *out, Str key, int *first) {
    buf_maybe_flush(out);
    if (*first) { *first = 0; buf_put(out, '"'); }
    else { buf_lit(out, ",\""); }
    buf_append_json_escaped(out, key.p, key.len);
    buf_lit(out, "\":\"");
}

static inline Str make_report_line3(Arena *a, const char *p1, Str s2, const char *p3, Str s4, const char *p5) {
    size_t l1 = 0; while (p1[l1]) l1++;
    size_t l3 = 0; while (p3[l3]) l3++;
    size_t l5 = 0; while (p5[l5]) l5++;
    uint32_t tot = (uint32_t)(l1 + s2.len + l3 + s4.len + l5);
    char *dst = arena_alloc_str(a, tot);
    size_t pos = 0;
    fast_copy(dst + pos, p1, l1); pos += l1;
    fast_copy(dst + pos, s2.p, s2.len); pos += s2.len;
    fast_copy(dst + pos, p3, l3); pos += l3;
    fast_copy(dst + pos, s4.p, s4.len); pos += s4.len;
    fast_copy(dst + pos, p5, l5);
    return (Str){ dst, tot };
}

// ---- Build One Site Case Directly Into JSON Output Buffer ----
static void build_case(Arena *a, CaseInput *ci, Buf *out, Buf *md_html_buf, Buf *md_tmp_buf, Buf *fold_buf) {
    // Report lines array
    uint32_t rep_cap = ci->num_files + ci->num_templates + 256;
    Str *report = (Str *)arena_alloc(a, rep_cap * sizeof(Str), 8);
    uint32_t num_report = 0;

    // 1. Parse templates in code-unit order of name
    if (ci->num_templates > 1) sort_raw_templates(ci->templates, (int)ci->num_templates);
    ParsedTemplate *parsed_tpls = (ParsedTemplate *)arena_alloc(a, (ci->num_templates + 1) * sizeof(ParsedTemplate), 8);
    StrMap tpl_map;
    strmap_init(a, &tpl_map, ci->num_templates + 8);
    uint32_t num_valid_tpls = 0;
    int any_escaped_html_field = 0;
    for (uint32_t i = 0; i < ci->num_templates; i++) {
        Str err;
        if (parse_template(a, ci->templates[i].name, ci->templates[i].src, &parsed_tpls[num_valid_tpls], &err)) {
            if (parsed_tpls[num_valid_tpls].uses_escaped_html_field) any_escaped_html_field = 1;
            strmap_put(&tpl_map, ci->templates[i].name, num_valid_tpls);
            num_valid_tpls++;
        } else {
            if (num_report < rep_cap) report[num_report++] = err;
        }
    }
    int direct_json_md = !any_escaped_html_field;

    // Strip trailing '/' from base_url
    Str base = ci->site_base_url;
    while (base.len > 0 && base.p[base.len - 1] == '/') base.len--;

    // 2. Sort files by path in code-unit order & split into md sources vs statics
    if (ci->num_files > 1) sort_raw_files(ci->files, 0, (int)ci->num_files - 1);

    PageObj *sources = (PageObj *)arena_alloc(a, (ci->num_files + 1) * sizeof(PageObj), 8);
    RawFile *statics = (RawFile *)arena_alloc(a, (ci->num_files + 1) * sizeof(RawFile), 8);
    uint32_t num_sources = 0, num_statics = 0;

    // 3. Front matter & validation
    for (uint32_t i = 0; i < ci->num_files; i++) {
        RawFile *rf = &ci->files[i];
        int is_md = rf->path.len >= 3 && rf->path.p[rf->path.len - 3] == '.' &&
                    rf->path.p[rf->path.len - 2] == 'm' && rf->path.p[rf->path.len - 1] == 'd';
        if (!is_md) {
            statics[num_statics++] = *rf;
            continue;
        }
        Str text = rf->normalized ? rf->content : normalize_cr_str(a, rf->content);
        FrontMatter fm;
        parse_front_matter(a, text, &fm);
        if (fm.error.len > 0) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "refused ", rf->path, ": ", fm.error, "");
            continue;
        }
        int is_post = rf->path.len >= 6 && memcmp(rf->path.p, "posts/", 6) == 0;
        if (rf->path.len >= 5 && memcmp(rf->path.p, "tags/", 5) == 0) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "refused ", rf->path, ": tags/ is the generator's own", STR_EMPTY, "");
            continue;
        }
        if (rf->path.len >= 5 && memcmp(rf->path.p, "page/", 5) == 0) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "refused ", rf->path, ": page/ is the generator's own", STR_EMPTY, "");
            continue;
        }
        if (is_post && !fm.has_date) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "refused ", rf->path, ": a post needs a date", STR_EMPTY, "");
            continue;
        }
        Str layout = fm.has_layout ? fm.layout : (is_post ? STR_LIT("post") : STR_LIT("page"));
        if (!strmap_get(&tpl_map, layout, NULL)) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "refused ", rf->path, ": layout \"", layout, "\" is no template that parses");
            continue;
        }
        if (fm.has_draft && fm.draft) {
            if (num_report < rep_cap) report[num_report++] = make_report_line3(a, "draft ", rf->path, "", STR_EMPTY, "");
            continue;
        }
        PageObj *po = &sources[num_sources++];
        memset(po, 0, sizeof(*po));
        po->src_path = rf->path;
        po->text = text;
        po->body = fm.body;
        po->layout = layout;
        po->title = fm.title;
        po->has_date = fm.has_date;
        po->date = fm.has_date ? fm.date : STR_EMPTY;
        po->date_long = fm.has_date ? long_date(a, fm.date) : STR_EMPTY;
        po->summary = fm.has_summary ? fm.summary : STR_EMPTY;
        po->slug = fm.has_slug ? fm.slug : slugify(a, fm.title);
        po->raw_tags = fm.tags;
        po->num_raw_tags = fm.num_tags;
        po->is_post = (uint8_t)is_post;
    }

    // 4. Permalinks (unique slug per dir; root reserves posts/tags/page)
    StrMap dir_to_taken;
    strmap_init(a, &dir_to_taken, 64);
    StrMap *dir_sets = (StrMap *)arena_alloc(a, (num_sources + 4) * sizeof(StrMap), 8);
    uint32_t num_dir_sets = 0;

    // Pre-create root "" dir set with RESERVED
    strmap_init(a, &dir_sets[0], 64);
    strmap_put(&dir_sets[0], STR_LIT("posts"), 1);
    strmap_put(&dir_sets[0], STR_LIT("tags"), 1);
    strmap_put(&dir_sets[0], STR_LIT("page"), 1);
    strmap_put(&dir_to_taken, STR_EMPTY, 0);
    num_dir_sets = 1;

    StrMap link_map;
    strmap_init(a, &link_map, num_sources + num_statics + 16);
    Str *link_urls = (Str *)arena_alloc(a, (num_sources + num_statics + 1) * sizeof(Str), 8);
    uint32_t num_link_urls = 0;

    for (uint32_t i = 0; i < num_sources; i++) {
        PageObj *po = &sources[i];
        Str dir = po->is_post ? STR_LIT("posts") : dir_of(po->src_path);
        uint32_t dset_idx = 0;
        if (!strmap_get(&dir_to_taken, dir, &dset_idx)) {
            dset_idx = num_dir_sets++;
            strmap_init(a, &dir_sets[dset_idx], po->is_post ? (num_sources + 16) : 64);
            strmap_put(&dir_to_taken, dir, dset_idx);
        }
        po->slug = unique_slug(a, po->slug, &dir_sets[dset_idx]);
        if (dir.len == 0) {
            po->out_dir = po->slug;
        } else {
            uint32_t odlen = dir.len + 1 + po->slug.len;
            char *od = arena_alloc_str(a, odlen);
            fast_copy(od, dir.p, dir.len);
            od[dir.len] = '/';
            fast_copy(od + dir.len + 1, po->slug.p, po->slug.len);
            po->out_dir = (Str){ od, odlen };
        }
        uint32_t ulen = base.len + 1 + po->out_dir.len + 1;
        char *ub = arena_alloc_str(a, ulen);
        fast_copy(ub, base.p, base.len);
        ub[base.len] = '/';
        fast_copy(ub + base.len + 1, po->out_dir.p, po->out_dir.len);
        ub[ulen - 1] = '/';
        po->url = (Str){ ub, ulen };
        strmap_put(&link_map, po->src_path, num_link_urls);
        link_urls[num_link_urls++] = po->url;
    }
    // Insert static paths into the exact same unified link_map!
    for (uint32_t i = 0; i < num_statics; i++) {
        Str sp = statics[i].path;
        uint32_t ulen = base.len + 1 + sp.len;
        char *ub = arena_alloc_str(a, ulen);
        fast_copy(ub, base.p, base.len);
        ub[base.len] = '/';
        fast_copy(ub + base.len + 1, sp.p, sp.len);
        strmap_put(&link_map, sp, num_link_urls);
        link_urls[num_link_urls++] = (Str){ ub, ulen };
    }

    // 5. Reading minutes (Markdown rendering deferred to streaming per-page pass for L1 cache & minimal page faults!)
    uint32_t broken_cap = num_sources * 4 + 256;
    Str *broken_list = (Str *)arena_alloc(a, broken_cap * sizeof(Str), 8);
    uint32_t num_broken = 0;
    StrMap broken_set;
    strmap_init(a, &broken_set, num_sources + 64);

    md_html_buf->len = 0;
    MdEnv env;
    env.arena = a;
    env.base_url = base;
    env.link_map = &link_map;
    env.link_urls = link_urls;
    env.broken_set = &broken_set;
    env.broken_list = broken_list;
    env.broken_count = &num_broken;
    env.broken_cap = broken_cap;
    env.direct_json = direct_json_md;

    for (uint32_t i = 0; i < num_sources; i++) {
        PageObj *po = &sources[i];
        uint32_t wc = count_words(po->body);
        uint32_t rm = (wc + 199) / 200;
        po->reading_minutes = rm < 1 ? 1 : rm;
    }

    // 6. Collections (posts newest first, then slug asc; newer/older links)
    PageObj **posts = (PageObj **)arena_alloc(a, (num_sources + 1) * sizeof(PageObj *), 8);
    uint32_t num_posts = 0;
    for (uint32_t i = 0; i < num_sources; i++) {
        if (sources[i].is_post) posts[num_posts++] = &sources[i];
    }
    if (num_posts > 1) sort_posts(posts, 0, (int)num_posts - 1);
    for (uint32_t k = 0; k < num_posts; k++) {
        posts[k]->post_idx = k;
        posts[k]->newer = (k == 0) ? NULL : posts[k - 1];
        posts[k]->older = (k + 1 == num_posts) ? NULL : posts[k + 1];
    }

    // 7. Tags (walk posts in collection order, then non-posts)
    uint32_t max_tags = num_sources * 5 + 32;
    TagObj *tags = (TagObj *)arena_alloc(a, max_tags * sizeof(TagObj), 8);
    uint32_t num_tags = 0;
    StrMap tag_by_slug;
    strmap_init(a, &tag_by_slug, 256);

    for (uint32_t k = 0; k < num_posts; k++) {
        PageObj *po = posts[k];
        if (po->num_raw_tags == 0) continue;
        po->tag_refs = (TagRef *)arena_alloc(a, po->num_raw_tags * sizeof(TagRef), 8);
        Str seen_slugs[64];
        uint32_t nseen = 0;
        for (uint32_t j = 0; j < po->num_raw_tags; j++) {
            Str tname = po->raw_tags[j];
            Str tslug = slugify(a, tname);
            int dup = 0;
            for (uint32_t u = 0; u < nseen; u++) if (str_eq(seen_slugs[u], tslug)) { dup = 1; break; }
            if (dup) continue;
            if (nseen < 64) seen_slugs[nseen++] = tslug;
            uint32_t tidx;
            if (!strmap_get(&tag_by_slug, tslug, &tidx)) {
                tidx = num_tags++;
                TagObj *t = &tags[tidx];
                memset(t, 0, sizeof(*t));
                t->name = tname;
                t->slug = tslug;
                uint32_t tulen = base.len + 6 + tslug.len + 1;
                char *tub = arena_alloc_str(a, tulen);
                fast_copy(tub, base.p, base.len);
                fast_copy(tub + base.len, "/tags/", 6);
                fast_copy(tub + base.len + 6, tslug.p, tslug.len);
                tub[tulen - 1] = '/';
                t->url = (Str){ tub, tulen };
                t->cap_posts = 16;
                t->posts = (PageObj **)arena_alloc(a, t->cap_posts * sizeof(PageObj *), 8);
                strmap_put(&tag_by_slug, tslug, tidx);
            }
            TagObj *t = &tags[tidx];
            if (t->num_posts == t->cap_posts) {
                uint32_t ncap = t->cap_posts * 2;
                PageObj **np = (PageObj **)arena_alloc(a, ncap * sizeof(PageObj *), 8);
                memcpy(np, t->posts, t->num_posts * sizeof(PageObj *));
                t->posts = np; t->cap_posts = ncap;
            }
            t->posts[t->num_posts++] = po;
            // Store TagRef (note: name/url are t->name, t->url — first spelling!)
            po->tag_refs[po->num_tag_refs++] = (TagRef){
                t->name, STR_EMPTY, STR_EMPTY,
                t->slug, STR_EMPTY, STR_EMPTY,
                t->url,  STR_EMPTY, STR_EMPTY,
                t
            };
        }
    }
    // Non-post page tags
    for (uint32_t i = 0; i < num_sources; i++) {
        PageObj *po = &sources[i];
        if (po->is_post || po->num_raw_tags == 0) continue;
        po->tag_refs = (TagRef *)arena_alloc(a, po->num_raw_tags * sizeof(TagRef), 8);
        Str seen_slugs[64];
        uint32_t nseen = 0;
        for (uint32_t j = 0; j < po->num_raw_tags; j++) {
            Str tname = po->raw_tags[j];
            Str tslug = slugify(a, tname);
            int dup = 0;
            for (uint32_t u = 0; u < nseen; u++) if (str_eq(seen_slugs[u], tslug)) { dup = 1; break; }
            if (dup) continue;
            if (nseen < 64) seen_slugs[nseen++] = tslug;
            uint32_t tidx;
            if (strmap_get(&tag_by_slug, tslug, &tidx)) {
                TagObj *t = &tags[tidx];
                po->tag_refs[po->num_tag_refs++] = (TagRef){
                    t->name, STR_EMPTY, STR_EMPTY,
                    t->slug, STR_EMPTY, STR_EMPTY,
                    t->url,  STR_EMPTY, STR_EMPTY,
                    t
                };
            } else {
                po->tag_refs[po->num_tag_refs++] = (TagRef){
                    tname, STR_EMPTY, STR_EMPTY,
                    tslug, STR_EMPTY, STR_EMPTY,
                    STR_EMPTY, STR_EMPTY, STR_EMPTY,
                    NULL
                };
            }
        }
    }
    // Pre-escape all strings on Site, Tags, and Sources ONCE!
    SiteObj site_obj;
    site_obj.title = ci->site_title;
    site_obj.title_ej = arena_html_json_escape(a, site_obj.title);
    site_obj.title_rj = arena_json_escape(a, site_obj.title);
    site_obj.base_url = base;
    site_obj.base_url_ej = arena_html_json_escape(a, base);
    site_obj.base_url_rj = arena_json_escape(a, base);

    for (uint32_t i = 0; i < num_tags; i++) {
        TagObj *t = &tags[i];
        t->name_ej = arena_html_json_escape(a, t->name);
        t->name_rj = arena_json_escape(a, t->name);
        t->slug_ej = t->slug;
        t->slug_rj = t->slug;
        t->url_ej = arena_html_json_escape(a, t->url);
        t->url_rj = arena_json_escape(a, t->url);
    }
    for (uint32_t i = 0; i < num_sources; i++) {
        PageObj *po = &sources[i];
        po->title_ej = arena_html_json_escape(a, po->title);
        po->title_rj = arena_json_escape(a, po->title);
        po->url_ej = arena_html_json_escape(a, po->url);
        po->url_rj = arena_json_escape(a, po->url);
        po->date_ej = po->date; // YYYY-MM-DD or ""
        po->date_rj = po->date;
        po->date_long_ej = po->date_long;
        po->date_long_rj = po->date_long;
        po->summary_ej = arena_html_json_escape(a, po->summary);
        po->summary_rj = arena_json_escape(a, po->summary);
        po->slug_ej = po->slug;
        po->slug_rj = po->slug;
        for (uint32_t j = 0; j < po->num_tag_refs; j++) {
            TagRef *tr = &po->tag_refs[j];
            if (tr->global_tag) {
                tr->name_ej = tr->global_tag->name_ej;
                tr->name_rj = tr->global_tag->name_rj;
                tr->slug_ej = tr->global_tag->slug_ej;
                tr->slug_rj = tr->global_tag->slug_rj;
                tr->url_ej  = tr->global_tag->url_ej;
                tr->url_rj  = tr->global_tag->url_rj;
            } else {
                tr->name_ej = arena_html_json_escape(a, tr->name);
                tr->name_rj = arena_json_escape(a, tr->name);
                tr->slug_ej = tr->slug;
                tr->slug_rj = tr->slug;
                tr->url_ej  = STR_EMPTY;
                tr->url_rj  = STR_EMPTY;
            }
        }
    }
    // Sort tags by count DESC, slug ASC (AFTER copying TagRef escaped strings since TagRef pointers aren't used after!)
    if (num_tags > 1) sort_tags(tags, (int)num_tags);

    uint32_t num_recent = ci->recent < 0 ? 0 : (uint32_t)ci->recent;
    if (num_recent > num_posts) num_recent = num_posts;

    // Pre-render Markdown ONLY for the `recent` posts (~5 posts, ~20KB) needed by site-constant folding in Step 8!
    for (uint32_t k = 0; k < num_recent; k++) {
        PageObj *po = posts[k];
        env.src_path = po->src_path;
        Str sdir = dir_of(po->src_path);
        env.num_dir_segs = 0;
        if (sdir.len > 0) {
            uint32_t st = 0;
            for (uint32_t u = 0; u <= sdir.len; u++) {
                if (u == sdir.len || sdir.p[u] == '/') {
                    if (env.num_dir_segs < 32) env.dir_segs[env.num_dir_segs++] = (Str){ sdir.p + st, u - st };
                    st = u + 1;
                }
            }
        }
        MdResult mr;
        render_markdown(po->body, &env, md_html_buf, md_tmp_buf, &mr);
        po->html = mr.html;
        po->excerpt = mr.excerpt;
        po->toc = mr.toc;
        po->num_toc = mr.num_toc;
    }
    size_t recent_md_end = md_html_buf->len;

    // 8. Pre-compile & partially evaluate every needed layout!
    RenderCtx cctx;
    memset(&cctx, 0, sizeof(cctx));
    cctx.site = &site_obj;
    cctx.all_tags = tags;
    cctx.num_all_tags = num_tags;
    cctx.recent = posts;
    cctx.num_recent = num_recent;
    cctx.direct_json_md = direct_json_md;
    cctx.total_posts = num_posts;
    cctx.arena = a;

    // Compiled layout cache for page layouts (indexed by valid template index)
    TNodeList *compiled_page_tpl = (TNodeList *)arena_alloc(a, (num_valid_tpls + 1) * sizeof(TNodeList), 8);
    uint8_t *has_compiled_page = (uint8_t *)arena_alloc(a, num_valid_tpls + 1, 1);
    memset(has_compiled_page, 0, num_valid_tpls + 1);

    uint16_t next_memo_id = 0;

    // Compile index, tag, tags layouts
    TNodeList compiled_index = {0}, compiled_tag = {0}, compiled_tags = {0};
    uint32_t idx_tidx, tag_tidx, tags_tidx;
    int has_index_tpl = strmap_get(&tpl_map, STR_LIT("index"), &idx_tidx);
    int has_tag_tpl   = strmap_get(&tpl_map, STR_LIT("tag"), &tag_tidx);
    int has_tags_tpl  = strmap_get(&tpl_map, STR_LIT("tags"), &tags_tidx);

    if (has_index_tpl) {
        TNodeList inl = {0};
        inline_and_prepare(a, &parsed_tpls[idx_tidx].nodes, &tpl_map, parsed_tpls, 0, &inl);
        fold_nodes(a, &inl, LM_INDEX, &cctx, fold_buf, NULL, 0, &next_memo_id, &compiled_index);
    }
    if (has_tag_tpl) {
        TNodeList inl = {0};
        inline_and_prepare(a, &parsed_tpls[tag_tidx].nodes, &tpl_map, parsed_tpls, 0, &inl);
        fold_nodes(a, &inl, LM_TAG, &cctx, fold_buf, NULL, 0, &next_memo_id, &compiled_tag);
    }
    if (has_tags_tpl) {
        TNodeList inl = {0};
        inline_and_prepare(a, &parsed_tpls[tags_tidx].nodes, &tpl_map, parsed_tpls, 0, &inl);
        fold_nodes(a, &inl, LM_TAGS, &cctx, fold_buf, NULL, 0, &next_memo_id, &compiled_tags);
    }

    // Share memo slot between index and tag if their memoized card bodies are structurally identical!
    if (has_index_tpl && has_tag_tpl && next_memo_id == 2) {
        TNode *m_idx = NULL, *m_tag = NULL;
        for (uint32_t i = 0; i < compiled_index.len; i++) if (compiled_index.items[i].kind == NK_MEMO_FOR) { m_idx = &compiled_index.items[i]; break; }
        for (uint32_t i = 0; i < compiled_tag.len; i++) if (compiled_tag.items[i].kind == NK_MEMO_FOR) { m_tag = &compiled_tag.items[i]; break; }
        if (m_idx && m_tag && str_eq(m_idx->text, m_tag->text) && tnodelist_equal(&m_idx->body, &m_tag->body)) {
            m_tag->memo_id = m_idx->memo_id;
            next_memo_id = 1;
        }
    }

    if (next_memo_id > 0 && num_posts > 0) {
        size_t mcnt = (size_t)next_memo_id * num_posts;
        cctx.card_memo = (Str *)arena_alloc(a, mcnt * sizeof(Str), 8);
        memset(cctx.card_memo, 0, mcnt * sizeof(Str));
    }

    // Track output keys ONLY if at least one static file ends with "index.html" (saves 1100 hashes & allocations!)
    int any_static_html = 0;
    for (uint32_t i = 0; i < num_statics; i++) {
        Str sp = statics[i].path;
        if (sp.len >= 10 && memcmp(sp.p + sp.len - 10, "index.html", 10) == 0) { any_static_html = 1; break; }
    }
    StrMap output_keys;
    if (any_static_html) strmap_init(a, &output_keys, num_sources + num_tags + 128);

    buf_put(out, '{');
    int first_kv = 1;

    // 9a. Render every source page (streaming Markdown per-page so md_html stays hot in L1 cache!)
    for (uint32_t i = 0; i < num_sources; i++) {
        PageObj *po = &sources[i];
        if (!(po->is_post && po->post_idx < num_recent)) {
            md_html_buf->len = recent_md_end;
            env.src_path = po->src_path;
            Str sdir = dir_of(po->src_path);
            env.num_dir_segs = 0;
            if (sdir.len > 0) {
                uint32_t st = 0;
                for (uint32_t u = 0; u <= sdir.len; u++) {
                    if (u == sdir.len || sdir.p[u] == '/') {
                        if (env.num_dir_segs < 32) env.dir_segs[env.num_dir_segs++] = (Str){ sdir.p + st, u - st };
                        st = u + 1;
                    }
                }
            }
            MdResult mr;
            render_markdown(po->body, &env, md_html_buf, md_tmp_buf, &mr);
            po->html = mr.html;
            po->toc = mr.toc;
            po->num_toc = mr.num_toc;
            if (po->is_post && mr.excerpt.len > 0) {
                char *ex = arena_alloc_str(a, mr.excerpt.len);
                fast_copy(ex, mr.excerpt.p, mr.excerpt.len);
                po->excerpt = (Str){ ex, mr.excerpt.len };
            } else {
                po->excerpt = mr.excerpt;
            }
        }
        uint32_t tidx = 0;
        strmap_get(&tpl_map, po->layout, &tidx);
        if (!has_compiled_page[tidx]) {
            TNodeList inl = {0};
            inline_and_prepare(a, &parsed_tpls[tidx].nodes, &tpl_map, parsed_tpls, 0, &inl);
            compiled_page_tpl[tidx] = (TNodeList){0};
            fold_nodes(a, &inl, LM_PAGE, &cctx, fold_buf, NULL, 0, &next_memo_id, &compiled_page_tpl[tidx]);
            has_compiled_page[tidx] = 1;
        }
        buf_maybe_flush(out);
        if (first_kv) { first_kv = 0; buf_put(out, '"'); } else buf_lit(out, ",\"");
        buf_append_json_escaped(out, po->out_dir.p, po->out_dir.len);
        buf_lit(out, "/index.html\":\"");
        if (any_static_html) {
            uint32_t klen = po->out_dir.len + 11;
            char *kbuf = arena_alloc_str(a, klen);
            fast_copy(kbuf, po->out_dir.p, po->out_dir.len);
            fast_copy(kbuf + po->out_dir.len, "/index.html", 11);
            strmap_put(&output_keys, (Str){ kbuf, klen }, 1);
        }
        cctx.page = po;
        cctx.has_posts = 0; cctx.pager = NULL; cctx.tag = NULL; cctx.depth = 0;
        render_compiled_nodes(out, &compiled_page_tpl[tidx], &cctx);
        buf_put(out, '"');
    }

    // 9b. Render index pages
    if (has_index_tpl) {
        uint32_t per = ci->per_page > 0 ? (uint32_t)ci->per_page : 10;
        uint32_t npages = (num_posts + per - 1) / per;
        if (npages < 1) npages = 1;
        cctx.page = NULL; cctx.tag = NULL; cctx.has_posts = 1;
        for (uint32_t k = 1; k <= npages; k++) {
            uint32_t st = (k - 1) * per;
            uint32_t en = st + per;
            if (en > num_posts) en = num_posts;
            cctx.posts = posts + st;
            cctx.num_posts = st < num_posts ? (en - st) : 0;

            char u1[512], u2[512], u3[512], okey_buf[64];
            PagerObj pg;
            pg.number = k; pg.total = npages;
            {
                Buf ub = { u1, 0, sizeof(u1), -1 };
                buf_put_str(&ub, base);
                if (k == 1) buf_put(&ub, '/');
                else { buf_lit(&ub, "/page/"); buf_put_u32(&ub, k); buf_put(&ub, '/'); }
                pg.url = (Str){ u1, (uint32_t)ub.len };
                pg.url_ej = arena_html_json_escape(a, pg.url);
                pg.url_rj = arena_json_escape(a, pg.url);
            }
            if (k == 1) {
                pg.newer_url = pg.newer_url_ej = pg.newer_url_rj = STR_EMPTY;
            } else {
                Buf ub = { u2, 0, sizeof(u2), -1 };
                buf_put_str(&ub, base);
                if (k - 1 == 1) buf_put(&ub, '/');
                else { buf_lit(&ub, "/page/"); buf_put_u32(&ub, k - 1); buf_put(&ub, '/'); }
                pg.newer_url = (Str){ u2, (uint32_t)ub.len };
                pg.newer_url_ej = arena_html_json_escape(a, pg.newer_url);
                pg.newer_url_rj = arena_json_escape(a, pg.newer_url);
            }
            if (k == npages) {
                pg.older_url = pg.older_url_ej = pg.older_url_rj = STR_EMPTY;
            } else {
                Buf ub = { u3, 0, sizeof(u3), -1 };
                buf_put_str(&ub, base);
                buf_lit(&ub, "/page/"); buf_put_u32(&ub, k + 1); buf_put(&ub, '/');
                pg.older_url = (Str){ u3, (uint32_t)ub.len };
                pg.older_url_ej = arena_html_json_escape(a, pg.older_url);
                pg.older_url_rj = arena_json_escape(a, pg.older_url);
            }
            cctx.pager = &pg; cctx.depth = 0;

            buf_maybe_flush(out);
            if (first_kv) { first_kv = 0; buf_put(out, '"'); } else buf_lit(out, ",\"");
            if (k == 1) {
                buf_lit(out, "index.html\":\"");
                if (any_static_html) strmap_put(&output_keys, STR_LIT("index.html"), 1);
            } else {
                buf_lit(out, "page/"); buf_put_u32(out, k); buf_lit(out, "/index.html\":\"");
                if (any_static_html) {
                    Buf kb = { okey_buf, 0, sizeof(okey_buf), -1 };
                    buf_lit(&kb, "page/"); buf_put_u32(&kb, k); buf_lit(&kb, "/index.html");
                    char *kc = arena_alloc_str(a, kb.len);
                    fast_copy(kc, okey_buf, kb.len);
                    strmap_put(&output_keys, (Str){ kc, (uint32_t)kb.len }, 1);
                }
            }
            render_compiled_nodes(out, &compiled_index, &cctx);
            buf_put(out, '"');
        }
    }

    // 9c. Render tag pages
    if (has_tag_tpl) {
        cctx.page = NULL; cctx.has_posts = 0; cctx.pager = NULL;
        for (uint32_t i = 0; i < num_tags; i++) {
            TagObj *t = &tags[i];
            buf_maybe_flush(out);
            if (first_kv) { first_kv = 0; buf_put(out, '"'); } else buf_lit(out, ",\"");
            buf_lit(out, "tags/"); buf_put_str(out, t->slug); buf_lit(out, "/index.html\":\"");
            if (any_static_html) {
                uint32_t klen = 5 + t->slug.len + 11;
                char *kc = arena_alloc_str(a, klen);
                fast_copy(kc, "tags/", 5);
                fast_copy(kc + 5, t->slug.p, t->slug.len);
                fast_copy(kc + 5 + t->slug.len, "/index.html", 11);
                strmap_put(&output_keys, (Str){ kc, klen }, 1);
            }
            cctx.tag = t; cctx.depth = 0;
            render_compiled_nodes(out, &compiled_tag, &cctx);
            buf_put(out, '"');
        }
    }

    // 9d. Render tags index page
    if (has_tags_tpl) {
        buf_maybe_flush(out);
        if (first_kv) { first_kv = 0; buf_put(out, '"'); } else buf_lit(out, ",\"");
        buf_lit(out, "tags/index.html\":\"");
        if (any_static_html) strmap_put(&output_keys, STR_LIT("tags/index.html"), 1);
        cctx.page = NULL; cctx.has_posts = 0; cctx.pager = NULL; cctx.tag = NULL; cctx.depth = 0;
        render_compiled_nodes(out, &compiled_tags, &cctx);
        buf_put(out, '"');
    }

    // 9e. Static files (check shadowing!)
    for (uint32_t i = 0; i < num_statics; i++) {
        RawFile *sf = &statics[i];
        if (any_static_html && strmap_get(&output_keys, sf->path, NULL)) {
            if (num_report < rep_cap) {
                report[num_report++] = make_report_line3(a, "shadowed ", sf->path, ": a built page has its path", STR_EMPTY, "");
            }
        } else {
            if (any_static_html) strmap_put(&output_keys, sf->path, 1);
            emit_json_kv_open(out, sf->path, &first_kv);
            buf_append_json_escaped(out, sf->content.p, sf->content.len);
            buf_put(out, '"');
        }
    }

    // 10. Build report (report + broken sorted in code-unit order, then summary line)
    uint32_t total_lines = num_report + num_broken;
    Str *all_lines = (Str *)arena_alloc(a, (total_lines + 1) * sizeof(Str), 8);
    if (num_report) memcpy(all_lines, report, num_report * sizeof(Str));
    if (num_broken) memcpy(all_lines + num_report, broken_list, num_broken * sizeof(Str));
    if (total_lines > 1) sort_strs(all_lines, 0, (int)total_lines - 1);

    emit_json_kv_open(out, STR_LIT("build-report.txt"), &first_kv);
    for (uint32_t i = 0; i < total_lines; i++) {
        buf_append_json_escaped(out, all_lines[i].p, all_lines[i].len);
        buf_lit(out, "\\n");
    }
    uint32_t num_pages = num_sources - num_posts;
    buf_lit(out, "built "); buf_put_u32(out, num_pages);
    buf_lit(out, " pages, "); buf_put_u32(out, num_posts);
    buf_lit(out, " posts, "); buf_put_u32(out, num_tags);
    buf_lit(out, " tags and "); buf_put_u32(out, num_statics);
    buf_lit(out, " static files\\n\"");

    buf_put(out, '}');
}

// ---- Parse /proc/self/environ only if /input/cases.json is not found ----
static int get_env_dir(const char *env_buf, size_t env_len, const char *key, size_t klen, char *dst, size_t dst_cap) {
    size_t i = 0;
    while (i < env_len) {
        size_t st = i;
        while (i < env_len && env_buf[i] != '\0') i++;
        size_t len = i - st;
        if (len > klen + 1 && memcmp(env_buf + st, key, klen) == 0 && env_buf[st + klen] == '=') {
            size_t vlen = len - (klen + 1);
            if (vlen < dst_cap) {
                fast_copy(dst, env_buf + st + klen + 1, vlen);
                dst[vlen] = '\0';
                return (int)vlen;
            }
        }
        i++;
    }
    return -1;
}

static int run_engine(void) {
    simd_init();

    char in_path[512];
    char out_path[512];
    fast_copy(in_path, "/input/cases.json", 18);
    fast_copy(out_path, "/output/answers.json", 21);

    long fd = sys_openat(AT_FDCWD, in_path, O_RDONLY, 0);
    if (fd < 0) {
        // Fallback: read MAKEFUN_INPUT_DIR and MAKEFUN_OUTPUT_DIR from /proc/self/environ
        long efd = sys_openat(AT_FDCWD, "/proc/self/environ", O_RDONLY, 0);
        if (efd >= 0) {
            char ebuf[65536];
            size_t elen = 0;
            while (elen < sizeof(ebuf)) {
                long r = sys_read((int)efd, ebuf + elen, sizeof(ebuf) - elen);
                if (r <= 0) break;
                elen += (size_t)r;
            }
            sys_close((int)efd);
            char idir[448], odir[448];
            int il = get_env_dir(ebuf, elen, "MAKEFUN_INPUT_DIR", 17, idir, sizeof(idir));
            if (il > 0) {
                fast_copy(in_path, idir, (size_t)il);
                fast_copy(in_path + il, "/cases.json", 12);
            }
            int ol = get_env_dir(ebuf, elen, "MAKEFUN_OUTPUT_DIR", 18, odir, sizeof(odir));
            if (ol > 0) {
                fast_copy(out_path, odir, (size_t)ol);
                fast_copy(out_path + ol, "/answers.json", 14);
            }
        }
        fd = sys_openat(AT_FDCWD, in_path, O_RDONLY, 0);
        if (fd < 0) return 1;
    }

    long fsize = sys_lseek((int)fd, 0, SEEK_END);
    if (fsize < 0) { sys_close((int)fd); return 2; }

    // Zero-copy mmap of input file directly from kernel page cache!
    const uint8_t *in_mem = NULL;
    if (fsize > 0) {
        in_mem = (const uint8_t *)sys_mmap(NULL, (size_t)fsize, PROT_READ, MAP_PRIVATE, (int)fd, 0);
        if ((long)in_mem < 0 && (long)in_mem > -4096) { sys_close((int)fd); return 3; }
    }
    sys_close((int)fd);

    long ofd = sys_openat(AT_FDCWD, out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) return 4;

    // Compact demand-paged anonymous workspace (only touched pages allocate RAM!)
    size_t out_cap = 8ULL << 20;     // 8 MiB streaming output buffer (flushed at 3 MiB & per case)
    size_t md_html_cap = 32ULL << 20;
    size_t md_tmp_cap = 4ULL << 20;
    size_t fold_cap = 4ULL << 20;
    size_t arena_cap = 128ULL << 20;
    size_t total_mem = out_cap + md_html_cap + md_tmp_cap + fold_cap + arena_cap;

    uint8_t *mem = (uint8_t *)sys_mmap(NULL, total_mem, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)mem < 0 && (long)mem > -4096) { sys_close((int)ofd); return 3; }

    Buf out     = { (char *)mem, 0, out_cap, (int)ofd };
    Buf md_html = { (char *)(mem + out_cap), 0, md_html_cap, -1 };
    Buf md_tmp  = { (char *)(mem + out_cap + md_html_cap), 0, md_tmp_cap, -1 };
    Buf fold_b  = { (char *)(mem + out_cap + md_html_cap + md_tmp_cap), 0, fold_cap, -1 };
    Arena arena = { mem + out_cap + md_html_cap + md_tmp_cap + fold_cap, 0, arena_cap };

    JsonParser jp = { in_mem, in_mem + (size_t)fsize, &arena };
    jp_skip_ws(&jp);
    buf_put(&out, '[');
    int case_idx = 0;
    if (jp.p < jp.end && *jp.p == '[') {
        jp.p++;
        while (jp.p < jp.end) {
            jp_skip_ws(&jp);
            if (*jp.p == ']') { jp.p++; break; }
            if (*jp.p == '{') {
                jp.p++;
                arena.pos = 0; // Fresh per-case scratch arena (zero leaks, hot cache!)
                CaseInput ci;
                int have_input = 0;
                while (jp.p < jp.end) {
                    jp_skip_ws(&jp);
                    if (*jp.p == '}') { jp.p++; break; }
                    Str k = jp_parse_string(&jp, 0);
                    jp_skip_ws(&jp);
                    if (jp.p < jp.end && *jp.p == ':') jp.p++;
                    if (STR_EQ(k, "input")) {
                        have_input = parse_case_input(&jp, &ci);
                    } else {
                        jp_skip_value(&jp);
                    }
                    jp_skip_ws(&jp);
                    if (jp.p < jp.end && *jp.p == ',') jp.p++;
                }
                if (have_input) {
                    if (case_idx > 0) buf_put(&out, ',');
                    build_case(&arena, &ci, &out, &md_html, &md_tmp, &fold_b);
                    buf_flush(&out); // Flush per case -> keeps peak output RAM <= 3 MB!
                    case_idx++;
                }
            }
            jp_skip_ws(&jp);
            if (jp.p < jp.end && *jp.p == ',') jp.p++;
        }
    }
    buf_put(&out, ']');
    buf_flush(&out);
    sys_close((int)ofd);
    return 0;
}

#ifdef STANDALONE_CLI
int main(void) {
    return run_engine();
}
#else
__attribute__((visibility("default")))
void *napi_register_module_v1(void *env, void *exports) {
    (void)env;
    int rc = run_engine();
    sys_exit_group(rc);
    return exports;
}
#endif
