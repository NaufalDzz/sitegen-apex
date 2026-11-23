// Clean, dependency-free ES module implementing makefun-site 1.0.0 semantics (run(input) -> output map).

const REPORT_FILE = 'build-report.txt';
const RESERVED = ['posts', 'tags', 'page'];
const MONTHS = [
  'January', 'February', 'March', 'April', 'May', 'June',
  'July', 'August', 'September', 'October', 'November', 'December',
];
const ESC_MAP = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
const ESC_RE = /[&<>"']/g;
const byCU = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

function escapeHtml(s) {
  return s.replace(ESC_RE, (c) => ESC_MAP[c]);
}

function slugify(text) {
  const plain = text.normalize('NFKD').replace(/\p{M}/gu, '');
  let out = '';
  let gap = false;
  for (const ch of plain) {
    const code = ch.charCodeAt(0);
    let keep = null;
    if (ch.length === 1) {
      if (code >= 65 && code <= 90) keep = String.fromCharCode(code + 32);
      else if ((code >= 97 && code <= 122) || (code >= 48 && code <= 57)) keep = ch;
    }
    if (keep === null) {
      gap = out.length > 0;
      continue;
    }
    if (gap) out += '-';
    gap = false;
    out += keep;
  }
  out = out.slice(0, 64).replace(/-+$/, '');
  return out === '' ? 'untitled' : out;
}

function uniqueSlug(slug, taken) {
  let out = slug;
  for (let n = 2; taken.has(out); n++) out = `${slug}-${n}`;
  taken.add(out);
  return out;
}

function longDate(date) {
  const y = date.slice(0, 4);
  const m = Number(date.slice(5, 7));
  const d = Number(date.slice(8, 10));
  return `${d} ${MONTHS[m - 1]} ${y}`;
}

function daysIn(y, m) {
  if (m === 2) return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0 ? 29 : 28;
  return m === 4 || m === 6 || m === 9 || m === 11 ? 30 : 31;
}

const FM_KEYS = new Set(['title', 'date', 'tags', 'draft', 'layout', 'slug', 'summary']);
const RE_LINE = /^([a-z_]+):(?: (.*))?$/;
const RE_DATE = /^(\d{4})-(\d{2})-(\d{2})$/;
const RE_NAME = /^[a-z0-9-]+$/;
const RE_SLUG = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const RE_TAG = /^[^"[\]]+$/;

function parseFmString(raw) {
  if (raw.startsWith('"')) {
    if (raw.length < 2 || !raw.endsWith('"')) return { error: 'a quoted string is not closed' };
    let out = '';
    for (let i = 1; i < raw.length - 1; i++) {
      const c = raw[i];
      if (c === '\\') {
        const n = raw[i + 1];
        if ((n !== '"' && n !== '\\') || i + 1 === raw.length - 1) {
          return { error: 'a quoted string holds an escape other than \\" or \\\\' };
        }
        out += n;
        i++;
      } else if (c === '"') {
        return { error: 'a quoted string holds a stray quote' };
      } else {
        out += c;
      }
    }
    return { value: out };
  }
  if (raw.startsWith('[')) return { error: 'a bare string opens on [' };
  return { value: raw };
}

function parseFrontMatter(text) {
  const lines = text.split('\n');
  if (lines[0] !== '---') return { error: 'no front matter' };
  let close = -1;
  for (let i = 1; i < lines.length; i++) {
    if (lines[i] === '---') { close = i; break; }
  }
  if (close === -1) return { error: 'the front matter is not closed' };
  const data = {};
  for (let i = 1; i < close; i++) {
    const line = lines[i];
    if (/^[ \t]*$/.test(line) || line.startsWith('#')) continue;
    const m = RE_LINE.exec(line);
    if (!m) return { error: `line ${i + 1} is not "key: value"` };
    const key = m[1];
    const raw = (m[2] ?? '').replace(/[ \t]+$/, '');
    if (!FM_KEYS.has(key)) return { error: `unknown key "${key}"` };
    if (Object.hasOwn(data, key)) return { error: `key "${key}" is given twice` };
    if (raw === '') return { error: `key "${key}" has no value` };
    if (key === 'title' || key === 'summary') {
      const s = parseFmString(raw);
      if ('error' in s) return { error: `key "${key}": ${s.error}` };
      data[key] = s.value;
    } else if (key === 'date') {
      const dm = RE_DATE.exec(raw);
      const y = dm ? Number(dm[1]) : 0;
      const mo = dm ? Number(dm[2]) : 0;
      const d = dm ? Number(dm[3]) : 0;
      if (!dm || y < 1970 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > daysIn(y, mo)) {
        return { error: 'key "date": not a date from 1970-01-01 to 2099-12-31 (YYYY-MM-DD)' };
      }
      data.date = raw;
    } else if (key === 'tags') {
      if (!raw.startsWith('[') || !raw.endsWith(']')) return { error: 'key "tags": not a list [a, b]' };
      const inner = raw.slice(1, -1);
      if (inner.trim() === '') {
        data.tags = [];
      } else {
        const items = inner.split(',').map((t) => t.trim());
        if (items.some((t) => t === '' || !RE_TAG.test(t))) {
          return { error: 'key "tags": an empty tag, or one holding " [ or ]' };
        }
        data.tags = items;
      }
    } else if (key === 'draft') {
      if (raw === 'true') data.draft = true;
      else if (raw === 'false') data.draft = false;
      else return { error: 'key "draft": true or false' };
    } else if (key === 'layout') {
      if (!RE_NAME.test(raw)) return { error: 'key "layout": not a template name' };
      data.layout = raw;
    } else if (key === 'slug') {
      if (!RE_SLUG.test(raw) || raw.length > 64) return { error: 'key "slug": not a slug' };
      data.slug = raw;
    }
  }
  if (!Object.hasOwn(data, 'title')) return { error: 'no title' };
  return { data, body: lines.slice(close + 1).join('\n') };
}

const RE_BLANK = /^[ \t]*$/;
const RE_FENCE_OPEN = /^```([A-Za-z0-9_+-]*)[ \t]*$/;
const RE_FENCE_CLOSE = /^```[ \t]*$/;
const RE_HEADING = /^(#{1,6})[ \t]+(.*?)[ \t]*$/;
const RE_HR = /^(?:---|\*\*\*)[ \t]*$/;
const RE_BULLET = /^[-*] (.*)$/;
const RE_ORDERED = /^(\d{1,9})\. (.*)$/;
const RE_CONT = /^ {2,}\S/;
const ESCAPABLE = new Set(['\\', '`', '*', '_', '[', ']', '(', ')', '#', '+', '-', '.', '!', '>']);
const trimLine = (l) => l.replace(/^[ \t]+|[ \t]+$/g, '');

function headingOf(line) {
  const m = RE_HEADING.exec(line);
  return m === null || m[2] === '' ? null : { depth: m[1].length, text: m[2] };
}
function endsPara(line) {
  return RE_BLANK.test(line) || RE_FENCE_OPEN.test(line) || headingOf(line) !== null || RE_HR.test(line);
}
function findUnescaped(s, what, from) {
  for (let j = from; j <= s.length - what.length; j++) {
    if (s[j] === '\\' && j + 1 < s.length && ESCAPABLE.has(s[j + 1])) { j++; continue; }
    if (s.startsWith(what, j)) return j;
  }
  return -1;
}
function findSingleStar(s, from) {
  for (let j = from; j < s.length; j++) {
    const c = s[j];
    if (c === '\\' && j + 1 < s.length && ESCAPABLE.has(s[j + 1])) { j++; continue; }
    if (c !== '*') continue;
    if (s[j + 1] === '*') { j++; continue; }
    return j;
  }
  return -1;
}
function renderInline(s, linkFn) {
  let out = '';
  let i = 0;
  while (i < s.length) {
    const c = s[i];
    if (c === '\\' && i + 1 < s.length && ESCAPABLE.has(s[i + 1])) {
      out += escapeHtml(s[i + 1]);
      i += 2;
      continue;
    }
    if (c === '`') {
      const j = s.indexOf('`', i + 1);
      if (j > i + 1) { out += `<code>${escapeHtml(s.slice(i + 1, j))}</code>`; i = j + 1; }
      else { out += '`'; i++; }
      continue;
    }
    if (c === '*') {
      if (s[i + 1] === '*') {
        const j = findUnescaped(s, '**', i + 2);
        if (j > i + 2) { out += `<strong>${renderInline(s.slice(i + 2, j), linkFn)}</strong>`; i = j + 2; }
        else { out += '**'; i += 2; }
        continue;
      }
      const j = findSingleStar(s, i + 1);
      if (j > i + 1) { out += `<em>${renderInline(s.slice(i + 1, j), linkFn)}</em>`; i = j + 1; }
      else { out += '*'; i++; }
      continue;
    }
    if (c === '[') {
      const k = findUnescaped(s, ']', i + 1);
      const m = k !== -1 && s[k + 1] === '(' ? s.indexOf(')', k + 2) : -1;
      const target = m === -1 ? '' : s.slice(k + 2, m);
      if (target !== '' && !/\s/.test(target)) {
        out += `<a href="${escapeHtml(linkFn(target))}">${renderInline(s.slice(i + 1, k), linkFn)}</a>`;
        i = m + 1;
        continue;
      }
      out += '[';
      i++;
      continue;
    }
    out += escapeHtml(c);
    i++;
  }
  return out;
}

function renderMarkdown(src, linkFn) {
  const lines = src.split('\n');
  const ids = new Set();
  const blocks = [];
  const toc = [];
  let excerpt = null;
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (RE_BLANK.test(line)) { i++; continue; }
    const f = RE_FENCE_OPEN.exec(line);
    if (f) {
      let body = '';
      i++;
      while (i < lines.length && !RE_FENCE_CLOSE.test(lines[i])) body += `${escapeHtml(lines[i++])}\n`;
      i++;
      const cls = f[1] === '' ? '' : ` class="language-${escapeHtml(f[1])}"`;
      blocks.push(`<pre><code${cls}>${body}</code></pre>\n`);
      continue;
    }
    const h = headingOf(line);
    if (h) {
      const id = uniqueSlug(slugify(h.text), ids);
      const html = renderInline(h.text, linkFn);
      blocks.push(`<h${h.depth} id="${id}">${html}</h${h.depth}>\n`);
      if (h.depth === 2 || h.depth === 3) toc.push({ level: h.depth, id, html });
      i++;
      continue;
    }
    if (RE_HR.test(line)) { blocks.push('<hr>\n'); i++; continue; }
    if (line.startsWith('>')) {
      const q = [];
      while (i < lines.length && lines[i].startsWith('>')) {
        const t = trimLine(lines[i++].replace(/^> ?/, ''));
        if (t !== '') q.push(t);
      }
      blocks.push(q.length === 0 ? '<blockquote>\n</blockquote>\n' : `<blockquote>\n<p>${renderInline(q.join('\n'), linkFn)}</p>\n</blockquote>\n`);
      continue;
    }
    const bm = RE_BULLET.exec(line);
    const om = bm === null ? RE_ORDERED.exec(line) : null;
    if (bm || om) {
      const ord = om !== null;
      const re = ord ? RE_ORDERED : RE_BULLET;
      const start = ord ? Number(om[1]) : 1;
      const items = [];
      while (i < lines.length) {
        const m = re.exec(lines[i]);
        if (m) { items.push([trimLine(ord ? m[2] : m[1])]); i++; }
        else if (RE_CONT.test(lines[i])) { items[items.length - 1].push(trimLine(lines[i++])); }
        else break;
      }
      const tag = ord ? 'ol' : 'ul';
      const open = ord && start !== 1 ? `<ol start="${start}">` : `<${tag}>`;
      blocks.push(`${open}\n${items.map((p) => `<li>${renderInline(p.join('\n'), linkFn)}</li>\n`).join('')}</${tag}>\n`);
      continue;
    }
    const p = [trimLine(line)];
    i++;
    while (i < lines.length && !endsPara(lines[i])) p.push(trimLine(lines[i++]));
    const html = renderInline(p.join('\n'), linkFn);
    if (excerpt === null) excerpt = html;
    blocks.push(`<p>${html}</p>\n`);
  }
  return { html: blocks.join(''), toc, excerpt: excerpt ?? '' };
}

const RE_PATH = /^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$/;
const RE_FOR = /^for ([A-Za-z_][A-Za-z0-9_]*) in (\S+)$/;
const RE_IF = /^if (not )?(\S+)$/;
const RE_INC = /^include "([a-z0-9-]+)"$/;

class TplErr extends Error {}

function parsePath(name, text) {
  if (!RE_PATH.test(text)) throw new TplErr(`template "${name}": ${JSON.stringify(text)} is not a path`);
  return text.split('.');
}

function parseTemplate(name, src) {
  const toks = [];
  let text = '';
  let i = 0;
  while (i < src.length) {
    const open = src.indexOf('{', i);
    if (open === -1 || open === src.length - 1) { text += src.slice(i); break; }
    const next = src[open + 1];
    if (next !== '{' && next !== '%') { text += src.slice(i, open + 1); i = open + 1; continue; }
    text += src.slice(i, open);
    if (next === '{') {
      const raw = src[open + 2] === '{';
      const close = src.indexOf(raw ? '}}}' : '}}', open + (raw ? 3 : 2));
      if (close === -1) throw new TplErr(`template "${name}": a ${raw ? '{{{' : '{{'} is not closed`);
      if (text !== '') toks.push({ t: 'text', text });
      text = '';
      toks.push({ t: raw ? 'raw' : 'var', path: parsePath(name, src.slice(open + (raw ? 3 : 2), close).trim()) });
      i = close + (raw ? 3 : 2);
      continue;
    }
    const close = src.indexOf('%}', open + 2);
    if (close === -1) throw new TplErr(`template "${name}": a {% is not closed`);
    const end = close + 2;
    const lineStart = src.lastIndexOf('\n', open - 1) + 1;
    const nl = src.indexOf('\n', end);
    const lineEnd = nl === -1 ? src.length : nl;
    const standalone = RE_BLANK.test(src.slice(lineStart, open)) && RE_BLANK.test(src.slice(end, lineEnd));
    if (standalone) text = text.slice(0, text.length - (open - lineStart));
    if (text !== '') toks.push({ t: 'text', text });
    text = '';
    toks.push({ t: 'tag', tag: src.slice(open + 2, close).trim() });
    i = standalone ? (nl === -1 ? src.length : nl + 1) : end;
  }
  if (text !== '') toks.push({ t: 'text', text });

  const root = { kind: 'root', body: [] };
  const stack = [root];
  let body = root.body;
  for (const tok of toks) {
    if (tok.t === 'text') body.push({ kind: 'text', text: tok.text });
    else if (tok.t === 'var' || tok.t === 'raw') body.push({ kind: tok.t, path: tok.path });
    else {
      const tag = tok.tag;
      const top = stack[stack.length - 1];
      let m;
      if ((m = RE_FOR.exec(tag)) !== null) {
        const node = { kind: 'for', name: m[1], path: parsePath(name, m[2]), body: [] };
        body.push(node); stack.push(node); body = node.body;
      } else if ((m = RE_IF.exec(tag)) !== null) {
        const node = { kind: 'if', not: m[1] !== undefined, path: parsePath(name, m[2]), then: [], else: null };
        body.push(node); stack.push(node); body = node.then;
      } else if (tag === 'else') {
        if (top.kind !== 'if' || top.else !== null) throw new TplErr(`template "${name}": an else outside an if`);
        top.else = []; body = top.else;
      } else if (tag === 'endfor' || tag === 'endif') {
        if (top.kind !== (tag === 'endfor' ? 'for' : 'if')) throw new TplErr(`template "${name}": an ${tag} with no open ${tag.slice(3)}`);
        stack.pop();
        const p = stack[stack.length - 1];
        body = p.kind === 'root' || p.kind === 'for' ? p.body : p.else ?? p.then;
      } else if ((m = RE_INC.exec(tag)) !== null) {
        body.push({ kind: 'include', name: m[1] });
      } else {
        throw new TplErr(`template "${name}": {% ${tag} %} is not a tag`);
      }
    }
  }
  if (stack.length > 1) throw new TplErr(`template "${name}": a ${stack[stack.length - 1].kind} is not closed`);
  return root.body;
}

function lookup(scope, path) {
  let s = scope;
  while (s !== null && !Object.hasOwn(s.vars, path[0])) s = s.parent;
  if (s === null) return undefined;
  let v = s.vars[path[0]];
  for (let k = 1; k < path.length; k++) {
    if (v === null || typeof v !== 'object' || !Object.hasOwn(v, path[k])) return undefined;
    v = v[path[k]];
  }
  return v;
}
function textOf(v) {
  if (typeof v === 'string') return v;
  if (typeof v === 'number') return String(v);
  if (typeof v === 'boolean') return v ? 'true' : 'false';
  if (Array.isArray(v)) return v.map(textOf).join(', ');
  return '';
}
function truthy(v) {
  if (Array.isArray(v)) return v.length > 0;
  return v !== undefined && v !== null && v !== false && v !== '' && v !== 0;
}
function renderNodes(nodes, scope, templates, depth) {
  let out = '';
  for (const n of nodes) {
    if (n.kind === 'text') out += n.text;
    else if (n.kind === 'var') out += escapeHtml(textOf(lookup(scope, n.path)));
    else if (n.kind === 'raw') out += textOf(lookup(scope, n.path));
    else if (n.kind === 'if') {
      const br = truthy(lookup(scope, n.path)) !== n.not ? n.then : n.else;
      if (br !== null) out += renderNodes(br, scope, templates, depth);
    } else if (n.kind === 'for') {
      const xs = lookup(scope, n.path);
      if (!Array.isArray(xs)) continue;
      for (let k = 0; k < xs.length; k++) {
        const vars = { [n.name]: xs[k], loop: { index: k + 1, first: k === 0, last: k === xs.length - 1, length: xs.length } };
        out += renderNodes(n.body, { vars, parent: scope }, templates, depth);
      }
    } else if (n.kind === 'include') {
      const t = templates.get(n.name);
      if (t !== undefined && depth < 8) out += renderNodes(t, scope, templates, depth + 1);
    }
  }
  return out;
}

function resolvePath(dir, rel) {
  const parts = dir === '' ? [] : dir.split('/');
  for (const seg of rel.split('/')) {
    if (seg === '' || seg === '.') continue;
    if (seg === '..') { if (parts.length === 0) return null; parts.pop(); }
    else parts.push(seg);
  }
  return parts.join('/');
}
const dirOf = (p) => (p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '');
const wordsOf = (t) => { const m = t.match(/\S+/g); return m === null ? 0 : m.length; };

export function run(input) {
  const templates = new Map();
  const report = [];
  for (const name of Object.keys(input.templates).sort(byCU)) {
    try { templates.set(name, parseTemplate(name, input.templates[name])); }
    catch (err) { if (!(err instanceof TplErr)) throw err; report.push(err.message); }
  }
  const base = input.site.base_url.replace(/\/+$/, '');
  const files = [...input.files].sort((a, b) => byCU(a.path, b.path));
  const sources = [];
  const statics = [];
  for (const f of files) {
    if (f.path.endsWith('.md')) sources.push({ path: f.path, text: f.content.replace(/\r\n?/g, '\n') });
    else statics.push({ path: f.path, content: f.content });
  }
  const kept = [];
  for (const s of sources) {
    const fm = parseFrontMatter(s.text);
    if ('error' in fm) { report.push(`refused ${s.path}: ${fm.error}`); continue; }
    s.data = fm.data; s.body = fm.body;
    s.post = s.path.startsWith('posts/');
    const own = RESERVED.find((d) => d !== 'posts' && s.path.startsWith(`${d}/`));
    if (own !== undefined) { report.push(`refused ${s.path}: ${own}/ is the generator's own`); continue; }
    if (s.post && !Object.hasOwn(s.data, 'date')) { report.push(`refused ${s.path}: a post needs a date`); continue; }
    const layout = s.data.layout ?? (s.post ? 'post' : 'page');
    if (!templates.has(layout)) { report.push(`refused ${s.path}: layout "${layout}" is no template that parses`); continue; }
    s.layout = layout;
    if (s.data.draft === true) { report.push(`draft ${s.path}`); continue; }
    kept.push(s);
  }
  const taken = new Map();
  const takenIn = (d) => { if (!taken.has(d)) taken.set(d, new Set(d === '' ? RESERVED : [])); return taken.get(d); };
  const bySource = new Map();
  for (const s of kept) {
    const dir = s.post ? 'posts' : dirOf(s.path);
    s.slug = uniqueSlug(s.data.slug ?? slugify(s.data.title), takenIn(dir));
    s.outDir = dir === '' ? s.slug : `${dir}/${s.slug}`;
    s.url = `${base}/${s.outDir}/`;
    bySource.set(s.path, s);
  }
  const staticPaths = new Set(statics.map((f) => f.path));
  const broken = new Set();
  for (const s of kept) {
    const dir = dirOf(s.path);
    const md = renderMarkdown(s.body, (target) => {
      if (/^[A-Za-z][A-Za-z0-9+.-]*:/.test(target) || target.startsWith('/') || target.startsWith('#')) return target;
      const hash = target.indexOf('#');
      const rel = hash === -1 ? target : target.slice(0, hash);
      const frag = hash === -1 ? '' : target.slice(hash);
      const p = rel === '' ? null : resolvePath(dir, rel);
      if (p !== null && bySource.has(p)) return `${bySource.get(p).url}${frag}`;
      if (p !== null && staticPaths.has(p)) return `${base}/${p}${frag}`;
      broken.add(`broken-link ${s.path} -> ${target}`);
      return target;
    });
    s.html = md.html; s.toc = md.toc; s.excerpt = md.excerpt;
  }
  const posts = kept.filter((s) => s.post).sort((a, b) => byCU(b.data.date, a.data.date) || byCU(a.slug, b.slug));
  posts.forEach((p, k) => { p.newer = k === 0 ? null : posts[k - 1]; p.older = k === posts.length - 1 ? null : posts[k + 1]; });
  const bySlug = new Map();
  const ownTags = (s) => { const seen = new Set(); return (s.data.tags ?? []).filter((n) => { const sl = slugify(n); return !seen.has(sl) && seen.add(sl); }); };
  for (const p of posts) {
    p.tagRefs = [];
    for (const name of ownTags(p)) {
      const slug = slugify(name);
      if (!bySlug.has(slug)) bySlug.set(slug, { name, slug, url: `${base}/tags/${slug}/`, posts: [] });
      const tag = bySlug.get(slug);
      tag.posts.push(p); p.tagRefs.push(tag);
    }
  }
  for (const s of kept) if (!s.post) s.tagRefs = ownTags(s).map((name) => bySlug.get(slugify(name)) ?? { name, slug: slugify(name), url: '', posts: [] });
  const tags = [...bySlug.values()].sort((a, b) => b.posts.length - a.posts.length || byCU(a.slug, b.slug));
  const linkOf = (s) => (s === null ? null : { title: s.data.title, url: s.url, date: s.data.date ?? '', date_long: s.data.date === undefined ? '' : longDate(s.data.date) });
  const cardOf = (s) => ({
    title: s.data.title, url: s.url, date: s.data.date ?? '',
    date_long: s.data.date === undefined ? '' : longDate(s.data.date),
    summary: s.data.summary ?? '', excerpt: s.excerpt,
    reading_minutes: Math.max(1, Math.ceil(wordsOf(s.body) / 200)),
    tags: s.tagRefs.map((t) => ({ name: t.name, slug: t.slug, url: t.url })),
  });
  const cards = new Map(posts.map((p) => [p, cardOf(p)]));
  const site = { title: input.site.title, base_url: base };
  const allTags = tags.map((t) => ({ name: t.name, slug: t.slug, url: t.url, count: t.posts.length }));
  const recent = posts.slice(0, input.site.recent).map((p) => cards.get(p));
  const render = (name, vars) => {
    const nodes = templates.get(name);
    return nodes === undefined ? null : renderNodes(nodes, { vars: { site, tags: allTags, recent, ...vars }, parent: null }, templates, 0);
  };
  const output = {};
  for (const s of kept) {
    output[`${s.outDir}/index.html`] = render(s.layout, {
      page: {
        ...cardOf(s), slug: s.slug, content: s.html,
        toc: s.toc.map((h) => ({ level: h.level, id: h.id, html: h.html })),
        newer: linkOf(s.newer ?? null), older: linkOf(s.older ?? null), is_post: s.post,
      },
    });
  }
  const per = input.site.per_page;
  const pages = Math.max(1, Math.ceil(posts.length / per));
  const idxUrl = (k) => (k === 1 ? `${base}/` : `${base}/page/${k}/`);
  for (let k = 1; k <= pages; k++) {
    const html = render('index', {
      posts: posts.slice((k - 1) * per, k * per).map((p) => cards.get(p)),
      pager: { number: k, total: pages, url: idxUrl(k), newer_url: k === 1 ? '' : idxUrl(k - 1), older_url: k === pages ? '' : idxUrl(k + 1) },
    });
    if (html !== null) output[k === 1 ? 'index.html' : `page/${k}/index.html`] = html;
  }
  for (const t of tags) {
    const html = render('tag', { tag: { name: t.name, slug: t.slug, url: t.url, count: t.posts.length, posts: t.posts.map((p) => cards.get(p)) } });
    if (html !== null) output[`tags/${t.slug}/index.html`] = html;
  }
  const tagsHtml = render('tags', {});
  if (tagsHtml !== null) output['tags/index.html'] = tagsHtml;
  for (const f of statics) {
    if (Object.hasOwn(output, f.path)) report.push(`shadowed ${f.path}: a built page has its path`);
    else output[f.path] = f.content;
  }
  const lines = [...report, ...broken].sort(byCU);
  lines.push(`built ${kept.filter((s) => !s.post).length} pages, ${posts.length} posts, ${tags.length} tags and ${statics.length} static files`);
  output[REPORT_FILE] = `${lines.join('\n')}\n`;
  return output;
}
