# SPDX-License-Identifier: GPL-2.0-only

CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wshadow -Wmissing-prototypes \
	  -Wstrict-prototypes -MMD -MP
CFLAGS += $(shell $(PKG_CONFIG) --cflags ncursesw)
CPPFLAGS += -Isrc -Ibuild
LDLIBS += $(shell $(PKG_CONFIG) --libs ncursesw)

# Optional sanitizer build.
# Set SANITIZE=address,undefined (or thread) to rebuild every object under the chosen sanitizer;
# flags ride in CFLAGS so they reach both compile and link.
# -fno-sanitize-recover makes any finding abort the run,
#  so CI fails loudly instead of logging and continuing.
# test-asan/test-ubsan/test-tsan targets below drive this from a clean tree.
ifdef SANITIZE
CFLAGS += -fsanitize=$(SANITIZE) -fno-omit-frame-pointer -fno-sanitize-recover=all
endif

SRCS := $(wildcard src/*.c) $(wildcard drivers/*.c)
OBJS := $(patsubst %.c,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

BIN := alloyctl

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

build/%.o: %.c build/default_art.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# Embed fallback mouse art so binary is location independent
build/default_art.h: defaults/mouse.txt tools/txt2c.sh
	@mkdir -p build
	sh tools/txt2c.sh alloy_default_mouse_art < $< > $@

# Unit tests build without ncurses and with mocked HID transport.
# Cases live one file per mouse under tests/drivers/ plus driver-independent cases under tests/core/;
# both trees are wildcarded, so new test file is picked up automatically
# (the runner walks linker section, see tests/test.h).
TEST_SRCS := $(wildcard tests/*.c) $(wildcard tests/core/*.c) \
	     $(wildcard tests/drivers/*.c) src/driver.c src/state.c \
	     $(wildcard drivers/*.c)
TEST_OBJS := $(patsubst %.c,build/test/%.o,$(TEST_SRCS))

# -Itests lets cases under tests/core/ and tests/drivers/ pull in the shared
# harness (test.h) and mock transport (mock_hid.h) that live in tests/
build/test/%.o: %.c build/default_art.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) -c -o $@ $<

build/test/run-tests: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

test: build/test/run-tests
	./build/test/run-tests

# Runtime sanitizer gates for the unit tests.
# Each rebuilds from clean tree because the instrumentation changes every object.
# ASan is paired with UBSan; TSan is separate (they are mutually exclusive).
test-asan:
	rm -rf build
	$(MAKE) SANITIZE=address,undefined test

test-ubsan:
	rm -rf build
	$(MAKE) SANITIZE=undefined test

test-tsan:
	rm -rf build
	$(MAKE) SANITIZE=thread test

# Memcheck the unit tests under Valgrind.
# Built without sanitizer (the two cannot coexist);
# leak or invalid access fails the run.
VALGRIND ?= valgrind
VALGRIND_FLAGS := --error-exitcode=1 --leak-check=full \
	--errors-for-leak-kinds=definite,indirect --track-origins=yes -q
test-valgrind:
	rm -rf build
	$(MAKE) build/test/run-tests
	$(VALGRIND) $(VALGRIND_FLAGS) ./build/test/run-tests

check-format:
	@fail=0; \
	for f in $$(git ls-files '*.c' '*.h'); do \
		if ! clang-format --dry-run -Werror $$f; then fail=1; fi; \
	done; \
	exit $$fail

format:
	clang-format -i $$(git ls-files '*.c' '*.h')

# Build the RST manual with Sphinx (strict; see Documentation/Makefile).
# Output lands in Documentation/_build/html/index.html.
htmldocs:
	$(MAKE) -C Documentation html

# Lint RST sources with sphinx-lint.
checkdocs:
	$(MAKE) -C Documentation lint

# Build the manual and serve it locally for preview.
# Override the port with SERVE_PORT=NNNN
docs-serve:
	$(MAKE) -C Documentation serve

# Local contributor-side patch quality gate (mirrors CI).
check-patch:
	scripts/check-patch

# .github/CODEOWNERS is generated from MAINTAINERS; never hand-edit it!
codeowners:
	scripts/get-maintainer --codeowners > .github/CODEOWNERS

# Fail if the committed CODEOWNERS drifted from MAINTAINERS (CI enforces this)
check-codeowners:
	@scripts/get-maintainer --codeowners | diff -u .github/CODEOWNERS - \
		|| { echo "check-codeowners: .github/CODEOWNERS is stale; run 'make codeowners'"; exit 1; }

# Release guard:
# Annotated tag on HEAD must equal v + the VERSION file, so release can never ship
# version string that disagrees with its tag.
# Silence means OK.
# Release workflow runs this before publishing.
check-version-tag:
	@tag=$$(git describe --exact-match --tags HEAD 2>/dev/null); \
	if [ -z "$$tag" ]; then \
		echo "check-version-tag: HEAD is not tagged"; exit 1; \
	fi; \
	ver=$$(cat VERSION); \
	if [ "$$tag" != "v$$ver" ]; then \
		echo "check-version-tag: tag $$tag != v$$ver (VERSION)"; exit 1; \
	fi

clean:
	rm -rf build $(BIN)
	$(MAKE) -C Documentation clean

-include $(DEPS) $(TEST_OBJS:.o=.d)

.PHONY: all test test-asan test-ubsan test-tsan test-valgrind \
	check-format format htmldocs checkdocs docs-serve check-patch \
	codeowners check-codeowners check-version-tag clean
