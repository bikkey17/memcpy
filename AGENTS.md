# AGENTS.md

The README covers building and what each test target does. The Makefile
comments and the file headers carry the rest of the detail. This file is only
the things that are easy to get wrong and are not written down elsewhere.

`src/` is the product: users copy those files into their own tree, so it stays
self-contained and free of harness code. Test scaffolding lives in `dev/`.

## Testing

`make check` is the gate, not `make test`, and it is only ~30s (clang) or ~40s
(gcc) from clean, so there is no reason to skip it. CI runs it for both
compilers on both architectures; keep both working, and note that their
sanitizer flags are spelled differently.

**Prove a check can fail before trusting it to pass.** Nearly every property
here is an absence -- "never writes outside `dest`", "never misaligns a store"
-- and a harness for an absence is very easy to write so that it cannot fail.
Two in this repo did exactly that before being caught:

- a guard-page test that could never fault, because pages are word-aligned so
  the edge word never crosses one;
- a concurrency test whose read-back was folded into the value just written,
  because the counter was not `volatile`.

Both reported success against deliberately broken code. So when you add or lean
on a check: break the thing on a scratch copy under `/tmp`, confirm the check
reports it, restore, and say in the commit or the reply that you did. Do not
edit `src/` in place for this.

## Traps

**The out-of-bounds access is deliberate. Do not "fix" it.** Every routine
loads a whole aligned `MWORD_t` at each end of the run, so all of them read
past both buffers and mask off the surplus afterwards. The plain (non-`T`)
routines also store that edge word back, writing outside `dest` and restoring
what was there. It is safe on the assumption stated in the headers -- MMU pages
are `MWORD_t` aligned, so a whole-word access cannot straddle into an unmapped
page. Two consequences:

- Never put `-fsanitize=address` on the normal build; it fails on every
  routine, reads first. `make test-asan` is the usable form -- stores only,
  `(T)` routines only. The Makefile explains the flags.
- A whole-buffer compare cannot tell "wrote the old value back" from "did not
  write". Catching a stray write needs ASan or a concurrent thread, which is
  what `dev/lssc_asan.c` and `dev/lssc_mt.c` are for.

**`head` and `heel` exist twice, with mirrored meanings.** In `lssc_lft.c` they
write the *low* n bytes of `val`, walking forward from a `dest` pointer. In
`lssc_lrt.c` they write the *high* n bytes, walking backward from an *end*
pointer. Both pairs are `static` and must stay so: making either external
collides at link time with the other. Anything needing them (`init_lft`,
`zero_lft`) lives in the same translation unit as the pair it uses.

**`d` and `s` are `MWORD_t *`.** `d + 1` advances `MWORD_SIZE` bytes; `d +
MWORD_SIZE` advances `MWORD_SIZE` squared. That has already caused one wild
write 240 bytes past the end of `dest`.

**Forward and reverse are line-by-line mirrors** (`lf`<->`lr`, `lft`<->`lrt`);
all shifts and pointer updates reverse. Fix one, check the other. Diffing the
pair is the fastest review available here.
