
# Build environment can be configured with the same environment
# variables as the top-level Makefile:
#   CC, CFLAGS, LDFLAGS, CPPFLAGS
#
# To run under a sanitizer:
#   cd .. && make clean && CFLAGS="-fsanitize=address,undefined -g -O1" make
#   cd tests && make clean && CFLAGS="-fsanitize=address,undefined -g -O1" make test

CFLAGS += -I.. -I../mbedtls/include -Wall -Wextra -Wno-sign-compare -g
LDFLAGS += -L..
LDLIBS = -lslink

# Auto-generated per-binary header dependencies (see the -include below),
# so a change to a header these tests include directly (slt.h,
# fixtures.h, ../libslink.h, ...) triggers a rebuild instead of `make`
# seeing an up-to-date-looking binary.
DEPFLAGS = -MMD -MP -MF $@.d

C_BINARIES = test_genutils test_globmatch test_payload test_slcd \
             test_streams test_statefile test_logging test_internals \
             test_netprims test_network

FUZZ_BINARIES = fuzz/fuzz_detect fuzz/fuzz_payload fuzz/fuzz_statefile \
                fuzz/fuzz_dtparse fuzz/fuzz_globmatch

all: $(C_BINARIES) slharness

# Binaries that need fixtures.c (synthetic miniSEED builders) linked in.
test_payload: test_payload.c fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_payload.c fixtures.c $(LDFLAGS) $(LDLIBS)

test_streams: test_streams.c fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_streams.c fixtures.c $(LDFLAGS) $(LDLIBS)

test_statefile: test_statefile.c fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_statefile.c fixtures.c $(LDFLAGS) $(LDLIBS)

test_slcd: test_slcd.c fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_slcd.c fixtures.c $(LDFLAGS) $(LDLIBS)

test_netprims: test_netprims.c fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_netprims.c fixtures.c $(LDFLAGS) $(LDLIBS)

# #includes ../slutils.c directly to reach its file-static helpers; see
# the comment at the top of test_internals.c for why this does not
# duplicate symbols with slutils.o in the archive.
test_internals: test_internals.c fixtures.c fixtures.h ../slutils.c ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_internals.c fixtures.c $(LDFLAGS) $(LDLIBS)

# #includes ../network.c directly to reach its file-static helpers; see
# the comment at the top of test_internals.c for why this does not
# duplicate symbols with network.o in the archive.
test_network: test_network.c ../network.c ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ test_network.c $(LDFLAGS) $(LDLIBS)

# Everything else is a single source file against the standard library.
test_genutils test_globmatch test_logging slharness : % : %.c ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

# No prerequisites are listed here on purpose: once ../libslink.a exists,
# plain file-timestamp comparison can't tell whether the library sources
# (or a header they include) have changed since. Marking this phony
# always re-invokes the parent build, which does its own -MMD/-MP-based
# staleness check and only actually recompiles/relinks what changed.
.PHONY: ../libslink.a
../libslink.a:
	$(MAKE) -C .. static

test check: all
	@if command -v python3 >/dev/null 2>&1; then \
	  python3 runtests.py $(ARGS); \
	else \
	  echo "python3 not found; cannot run tests/runtests.py"; \
	  exit 1; \
	fi

# Long-running mutation-based dynamic-testing drivers (see
# fuzz/fuzzcommon.h) -- not part of `all`/`test`, since they are meant to
# be run by hand for a long time (or in CI as a separate time-boxed job),
# typically under a sanitizer: run each with `--iterations N --seed S`.
fuzz: $(FUZZ_BINARIES)

fuzz/fuzz_detect: fuzz/fuzz_detect.c fuzz/fuzzcommon.h fixtures.c fixtures.h ../slutils.c ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fuzz/fuzz_detect.c fixtures.c $(LDFLAGS) $(LDLIBS)

fuzz/fuzz_payload: fuzz/fuzz_payload.c fuzz/fuzzcommon.h fixtures.c fixtures.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fuzz/fuzz_payload.c fixtures.c $(LDFLAGS) $(LDLIBS)

fuzz/fuzz_statefile: fuzz/fuzz_statefile.c fuzz/fuzzcommon.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fuzz/fuzz_statefile.c $(LDFLAGS) $(LDLIBS)

fuzz/fuzz_dtparse: fuzz/fuzz_dtparse.c fuzz/fuzzcommon.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fuzz/fuzz_dtparse.c $(LDFLAGS) $(LDLIBS)

fuzz/fuzz_globmatch: fuzz/fuzz_globmatch.c fuzz/fuzzcommon.h ../libslink.a
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ fuzz/fuzz_globmatch.c $(LDFLAGS) $(LDLIBS)

clean:
	@$(RM) $(C_BINARIES) slharness
	@$(RM) $(addsuffix .d,$(C_BINARIES) slharness)
	@$(RM) $(FUZZ_BINARIES)
	@$(RM) -r __pycache__ slmock/__pycache__
	@$(RM) -rf *.dSYM
	@echo "Test binaries clean."

.PHONY: all test check clean fuzz

-include $(addsuffix .d,$(C_BINARIES) slharness)
