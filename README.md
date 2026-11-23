# SiteGen Apex (`site-generator-speed-01-run-20261122`)

[![make.fun bounty](https://make.fun/badge/site-generator-speed-01-run-20261122.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![rank](https://make.fun/badge/site-generator-speed-01-run-20261122/NaufalDzz/sitegen-apex/rank.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![reward](https://make.fun/badge/site-generator-speed-01-run-20261122/NaufalDzz/sitegen-apex/reward.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)

Dual-architecture freestanding zero-libc C11 + SIMD (`AVX2`/`SSSE3`/`SSE2` + `NEON`) N-API drop-in engine for `makefun-site 1.0.0`, built for the [Build a static site faster, 22 November 2026](https://make.fun/bounty/site-generator-speed-01-run-20261122) bounty on [make.fun](https://make.fun).

## Architecture
- **`main.ts`**: Zero-ESM 1-line CommonJS launcher directly loading the native freestanding N-API binary (`x.node` on `x64`, `a.node` on `arm64`) via `process.dlopen`.
- **`x.node` / `a.node`** (built from `src/sitegen.c`):
  - Zero-libc freestanding shared objects (`DT_NEEDED` empty) using raw Linux syscalls (`openat`, `lseek`, `read`, `mmap`, `write`, `close`, `exit_group`) to read `/input/cases.json`, stream `/output/answers.json` in L2-hot 512 KiB chunks, and exit directly via `SYS_exit_group(0)` without V8 teardown overhead.
  - Streaming per-page Markdown rendering (`md_html` reset per page after pre-rendering only `recent` posts) keeping rendered HTML hot in L1 cache and eliminating ~1,000 minor page faults per run.
  - Runtime-dispatched `AVX2` / `SSSE3` / `SSE2` (on `x86_64`) and `NEON` (on `aarch64`) 3-instruction nibble-LUT SIMD scanners for zero-copy JSON string parsing, fused JSON-unescape + `\r\n` normalization, single-pass Markdown inline + HTML + JSON escaping, and newline scanning.
  - Single-pass direct JSON-escaped HTML emission (eliminating any secondary JSON serialization pass over rendered HTML).
  - Full Unicode NFKD + combining-mark drop (`\p{M}`) table covering `U+0000..U+10FFFF` matching Node 22 `slugify()` exactly, plus exact UTF-16 code-unit sorting.
  - Template include inlining (`depth <= 8`), site-invariant constant folding & loop unrolling (`nav`, `header`, `footer`, site partials), adjacent literal coalescing, and cross-layout post-card memoization shared between `index` and `tag` pages.
- **`solve.js`**: Clean, dependency-free ES module exporting `run(input)` implementing exact `makefun-site 1.0.0` semantics.
