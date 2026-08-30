# File name conventions
#
# File names are case sensitive for Makefile conventions, but there must be
# no clashes (i.e. no x.c and x.C) on case-insensitive file systems!
#
# *.c        - C11 source code file
# *.tests.c  - C11 source code file containing a test driver (main)
# *.h        - C11 included file
#
# Every *.tests.c builds into its own binary, linked against Unity and all
# the non-test sources.
#
# Maintained with Opus 5

PRJ_DIR        := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

SRC_DIR        := $(PRJ_DIR)/src
DEV_DIR        := $(PRJ_DIR)/dev
UNITY_DIR      := $(DEV_DIR)/unity
OUT_DIR        := $(PRJ_DIR)/out

# CC has a built-in default (cc), so `?=` would never take effect; test the
# origin instead so the environment and the command line still win.
ifeq ($(origin CC),default)
    CC         := clang
endif

OPT            ?= -O3

# Spare slot for whatever the variant targets below need to inject: a
# sanitizer, or a -DMWORD_SIZE/-DMMIN_ALIGN pair. Empty for a normal build,
# and part of C_FLAGS so it reaches every compile and the link.
XFLAGS         ?=

C_FLAGS        := -std=c11 $(OPT) -g -Wall -Wshadow \
                  -Werror=unused-result \
                  -Werror=incompatible-pointer-types \
                  -I$(SRC_DIR) $(XFLAGS)

# Unity is compiled without setjmp/longjmp and without the math helpers: this
# is freestanding-ish code and a failing assertion should just abort the test.
C_FLAGS_TEST   := -I$(UNITY_DIR) \
                  -DUNITY_EXCLUDE_SETJMP_H \
                  -DUNITY_EXCLUDE_MATH_H \
                  -DUNITY_INCLUDE_PRINT_FORMATTED

C_FILES_ALL    := $(wildcard $(SRC_DIR)/*.c)
C_FILES_TEST   := $(filter %.tests.c, $(C_FILES_ALL))
C_FILES        := $(filter-out %.tests.c, $(C_FILES_ALL))

OBJ_FILES      := $(C_FILES:$(SRC_DIR)/%.c=$(OUT_DIR)/%.o)
UNITY_OBJ      := $(OUT_DIR)/unity.o
LINK_OBJS      := $(OBJ_FILES) $(UNITY_OBJ)
TEST_BINS      := $(C_FILES_TEST:$(SRC_DIR)/%.tests.c=$(OUT_DIR)/%.tests)
DEP_FILES      := $(LINK_OBJS:%.o=%.d) $(TEST_BINS:%=%.d)

.PHONY: all test check test-san test-asan test-configs test-mt clean

all: test

# Each binary is run right after it is built, so a failure stops the build
# with a non-zero exit status -- which is all CI needs to see.
test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== Running $$(basename $$t) ==="; $$t || exit 1; done

# What CI should run. `test` alone only covers the host's own MWORD_SIZE /
# MMIN_ALIGN and trusts the compiler about alignment; the rest close that gap.
check: test test-san test-asan test-configs test-mt

# -fsanitize=undefined catches the misaligned sub-word store this code would
# otherwise only fault on at run time on a -mstrict-align target, where none
# of us are testing.
#
# NOT address, not here. Every routine loads a whole aligned MWORD_t at each
# end of the run, so all of them read past both buffers by design -- the edge
# word reaches beyond and the surplus bits are masked off afterwards. The
# plain routines also store that edge word back, writing outside dest as their
# headers document. ASan cannot tell any of that from a real overrun, so over
# the whole suite it fails on everything and would be noise, not a gate. (The
# code is safe on the assumption stated in the headers: MMU pages are MWORD_t
# aligned, so a whole-word access can never straddle into an unmapped page.)
# ASan is still worth having on narrower terms -- see test-asan below.
test-san:
	@echo "=== Sanitizer build ==="
	@$(MAKE) --no-print-directory test OUT_DIR=$(OUT_DIR)/san OPT=-O1 \
	         XFLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined"

# ASan, made usable by confining it to the (T) routines and to stores.
#
# -asan-instrument-reads=false is what unlocks this: every routine here reads
# past both buffers by design (see test-san above), so with reads instrumented
# ASan aborts on all of them. With only stores checked, what remains is
# precisely the promise the (T) routines make -- and the shadow map catches
# the store itself on every call, where lssc_mt can only catch one that a
# neighbouring thread happens to interleave with. The plain routines are not
# in that harness because they would abort it, correctly.
#
# Both compilers can turn reads off, with different spellings, so this target
# works either way. It compiles the sources rather than reusing $(OBJ_FILES)
# because those are built without instrumentation.
CC_IS_CLANG := $(shell $(CC) --version 2>/dev/null | head -1 | grep -ci clang)

ifeq ($(CC_IS_CLANG),0)
    ASAN_READS := --param=asan-instrument-reads=0
else
    ASAN_READS := -mllvm -asan-instrument-reads=false
endif

ASAN_FLAGS := -fsanitize=address $(ASAN_READS)

test-asan: $(OUT_DIR)/lssc_asan
	@echo "=== Running lssc_asan ((T) routines, stores only) ==="
	@$(OUT_DIR)/lssc_asan

$(OUT_DIR)/lssc_asan: $(DEV_DIR)/lssc_asan.c $(C_FILES) $(PRJ_DIR)/Makefile
	@mkdir -p $(@D)
	$(CC) $(C_FLAGS) $(ASAN_FLAGS) -MMD -MP -MF $@.d $< $(C_FILES) -o $@

# The alignment maths branches on MWORD_SIZE and MMIN_ALIGN, so the host's
# pair exercises one column of the table. These override the CPU detection in
# lssc.h to walk the rest, host-native but with the other geometries.
CONFIGS := 4/4 4/1 8/8 8/4 16/8 16/16 2/2 2/1

test-configs:
	@for c in $(CONFIGS); do \
	    w=$${c%/*}; a=$${c#*/}; \
	    echo "=== MWORD_SIZE=$$w MMIN_ALIGN=$$a ==="; \
	    $(MAKE) --no-print-directory test OUT_DIR=$(OUT_DIR)/cfg$$w-$$a \
	            XFLAGS="-DMWORD_SIZE=$$w -DMMIN_ALIGN=$$a" || exit 1; \
	done

# The (T) variants promise not to touch bytes outside dest. That is invisible
# to a single-threaded test -- the others put the same value back -- so it
# takes a concurrent writer owning the neighbouring bytes to observe.
test-mt: $(OUT_DIR)/lssc_mt
	@echo "=== Running lssc_mt (concurrent neighbour) ==="
	@$(OUT_DIR)/lssc_mt

$(OUT_DIR)/lssc_mt: $(DEV_DIR)/lssc_mt.c $(OBJ_FILES) $(PRJ_DIR)/Makefile
	@mkdir -p $(@D)
	$(CC) $(C_FLAGS) -pthread -MMD -MP -MF $@.d $< $(OBJ_FILES) -o $@

clean:
	rm -rf $(OUT_DIR)

# The library sources are built without the Unity include path on purpose:
# they must stay compilable on their own, since users just copy them.
$(OBJ_FILES): $(OUT_DIR)/%.o: $(SRC_DIR)/%.c $(PRJ_DIR)/Makefile
	@mkdir -p $(@D)
	$(CC) $(C_FLAGS) -MMD -MP -c $< -o $@

$(UNITY_OBJ): $(UNITY_DIR)/unity.c $(PRJ_DIR)/Makefile
	@mkdir -p $(@D)
	$(CC) $(C_FLAGS) $(C_FLAGS_TEST) -MMD -MP -c $< -o $@

# -MF is explicit here: left to itself the compiler derives the dep file from
# the output name by dropping the last suffix, so "x.tests" would yield "x.d"
# and collide with the dep file of a src/x.c.
$(TEST_BINS): $(OUT_DIR)/%.tests: $(SRC_DIR)/%.tests.c $(LINK_OBJS) $(PRJ_DIR)/Makefile
	@mkdir -p $(@D)
	$(CC) $(C_FLAGS) $(C_FLAGS_TEST) -MMD -MP -MF $@.d $< $(LINK_OBJS) -o $@

-include $(DEP_FILES)

print.%:
	@echo $($*)
