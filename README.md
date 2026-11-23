# SiteGen Apex (`site-generator-speed-01-run-20261122`)

[![make.fun bounty](https://make.fun/badge/site-generator-speed-01-run-20261122.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![rank](https://make.fun/badge/site-generator-speed-01-run-20261122/NaufalDzz/sitegen-apex/rank.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)
[![reward](https://make.fun/badge/site-generator-speed-01-run-20261122/NaufalDzz/sitegen-apex/reward.svg)](https://make.fun/bounty/site-generator-speed-01-run-20261122)

Dual-architecture freestanding zero-libc C11 + SIMD (`AVX2`/`SSSE3`/`SSE2` + `NEON`) N-API drop-in engine for `makefun-site 1.0.0`, built for the [Build a static site faster, 22 November 2026](https://make.fun/bounty/site-generator-speed-01-run-20261122) bounty on [make.fun](https://make.fun).

## Architecture
- **`package.json` + `main.ts`**: Zero-ESM CommonJS (`"type":"commonjs"`) 1-line launcher directly loading the native freestanding binary (`x.node` on `x64`, `a.node` on `arm64`) via `process.dlopen`.
- **`x.node` / `a.node`** (built from `src/sitegen.c`):
  - Zero-libc, zero-PLT, zero-dynsym freestanding shared objects (`DT_NEEDED` empty, `-Bsymbolic`, `DT_INIT_ARRAY` constructor runs inside `dlopen` before `dlsym` or N-API registration) using zero-copy `mmap` input reading, a compact 12 MiB demand-paged `.bss` workspace, L2-hot 256 KiB output streaming, and direct `SYS_exit_group(0)` exit without V8 teardown overhead.
  - Streaming per-page Markdown rendering (`md_html` reset per page after pre-rendering only `recent` posts) keeping rendered HTML hot in L1 cache and minimizing page faults.
  - Runtime-dispatched `AVX2` / `SSSE3` / `SSE2` (on `x86_64`) and `NEON` (on `aarch64`) 3-instruction nibble-LUT SIMD scanners for 32-byte speculative-store JSON string unescaping + `\r\n` normalization, single-pass Markdown inline + HTML + JSON escaping, SIMD word counting, and newline scanning.
  - Single-pass direct JSON-escaped HTML emission (eliminating any secondary JSON serialization pass over rendered HTML).
  - Full Unicode NFKD + combining-mark drop (`\p{M}`) table covering `U+0000..U+10FFFF` matching Node 22 `slugify()` exactly, plus exact UTF-16 code-unit sorting.
  - Template include inlining (`depth <= 8`), site-invariant constant folding & loop unrolling (`nav`, `header`, `footer`, site partials), adjacent literal coalescing, and cross-layout post-card memoization shared between `index` and `tag` pages.
- **`solve.mjs`**: Clean, dependency-free ES module exporting `run(input)` implementing exact `makefun-site 1.0.0` semantics.
