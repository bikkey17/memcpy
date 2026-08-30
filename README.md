## memcpy

[![pipeline status](https://gitlab.com/bikkey17/memcpy/badges/master/pipeline.svg)](https://gitlab.com/bikkey17/memcpy/-/commits/master)

`memcpy`/`memset`/`bzero` equivalents that align memory access so they work without MMU.

| Functionality | Endianness | Direction | Thread-Safe | Implemented by |
|---------------|------------|-----------|-------------|----------------|
| `memcpy`      | little     | forward   | no          | `lssc_lf`      |
| `memcpy`      | little     | forward   | yes         | `lssc_lft`     |
| `memcpy`      | little     | reverse   | no          | `lssc_lr`      |
| `memcpy`      | little     | reverse   | yes         | `lssc_lrt`     |
| `memset`      | little     | (forward) | no          | `init_lf`      |
| `memset`      | little     | (forward) | yes         | `init_lft`     |
| `bzero`       | little     | (forward) | no          | `zero_lf`      |
| `bzero`       | little     | (forward) | yes         | `zero_lft`     |
| `memcpy`      | big        | forward   | no          | TBD            |
| `memcpy`      | big        | forward   | yes         | TBD            |
| `memcpy`      | big        | reverse   | no          | TBD            |
| `memcpy`      | big        | reverse   | yes         | TBD            |

`memmove` is trivial

    void *memmove(void *dest, const void *src, size_t n)
    {
        if (src < dest)
            lssc_lrt(dest, src, n);
        else
            lssc_lft(dest, src, n);
        return dest;
    }

## Build/Test

    make            # build and run the unit tests (the default target)
    make check      # everything CI runs
    make clean

**Compilers**: `clang` (default), `gcc` (`make CC=gcc`)


| variable  | default | notes                                                    |
|-----------|---------|----------------------------------------------------------|
| `CC`      | `clang` | `CC=gcc` for GCC                                         |
| `OPT`     | `-O3`   | passed at start of CC command line                       |
| `OUT_DIR` | `out/`  |                                                          |
| `XFLAGS`  |         | custom CC options, passed at the end of the command line |


| target         | effect                                                  |
|----------------|---------------------------------------------------------|
| `all`          | `test`                                                  |
| `check`        | all `test*` targets, ~30s from clean                     |
| `clean`        | empties `OUT_DIR`                                       |
| `print.VAR`    | prints Makefile variable `VAR` (for Makefile debugging) |
| `test`         | correctness, every alignment pair, host geometry only   |
| `test-configs` | the same suite for 8 `MWORD_SIZE`/`MMIN_ALIGN` pairs    |
| `test-san`     | UBSan over the suite                                    |
| `test-asan`    | ASan, stores only, thread-safe routines only            |
| `test-mt`      | concurrent neighbour: the thread-safety promise         |

The sanitizer targets need runtimes, which are separate packages on Debian:
`libasan8` and `libubsan1` for `gcc`, `libclang-rt-dev` for `clang`.

## CI

CI runs `make check` for {`x86_64`, `aarch64`} x {`gcc`, `clang`}.
