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

# Driver selection (opt-in)
# Each directory under drivers/ is self-contained driver that registers itself
# through the alloy_drivers ELF section, so the drivers built into the binary
# are exactly the driver objects linked in - dropping one drops its code and its embedded art.
# DRIVERS restricts that set; empty (the default) builds every driver, so plain `make` is unchanged.
#   make DRIVERS="steelseries_rival3_gen2"   only this driver
#   make list-drivers                        show the valid names
# Test binary always links every driver (see TEST_SRCS below).
ALL_DRIVERS := $(sort $(notdir $(patsubst %/,%,$(wildcard drivers/*/))))
ifeq ($(strip $(DRIVERS)),)
SEL_DRIVERS := $(ALL_DRIVERS)
else
SEL_DRIVERS := $(strip $(DRIVERS))
UNKNOWN_DRIVERS := $(filter-out $(ALL_DRIVERS),$(SEL_DRIVERS))
ifneq ($(UNKNOWN_DRIVERS),)
$(error unknown driver(s): $(UNKNOWN_DRIVERS); available: $(ALL_DRIVERS) \
	(run 'make list-drivers'))
endif
endif

SRCS := $(wildcard src/*.c) \
	$(foreach d,$(SEL_DRIVERS),$(wildcard drivers/$(d)/*.c))
OBJS := $(patsubst %.c,build/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

BIN := alloyctl

# Install locations (override on the make command line as needed)
PREFIX ?= /usr/local
DESTDIR ?=
BINDIR ?= $(PREFIX)/bin
UDEVDIR ?= /usr/lib/udev/rules.d

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Art rules exist for every driver so the all-driver test binary can link them;
# main binary only depends on - and thus embeds - the selected drivers' art
ALL_ART_DRIVERS := $(patsubst %_art.txt,%,$(notdir $(wildcard drivers/*/*_art.txt)))
ALL_ART_HDRS := $(patsubst %,build/art_%.h,$(ALL_ART_DRIVERS))
ART_HDRS := $(patsubst %,build/art_%.h,$(filter $(SEL_DRIVERS),$(ALL_ART_DRIVERS)))

define ART_RULE
build/art_$(1).h: drivers/$(1)/$(1)_art.txt tools/txt2c.sh
	@mkdir -p build
	sh tools/txt2c.sh alloy_art_$(1) < $$< > $$@
endef
$(foreach d,$(ALL_ART_DRIVERS),$(eval $(call ART_RULE,$(d))))

build/%.o: %.c build/default_art.h $(ART_HDRS)
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
	     src/accel_transform.c $(wildcard drivers/*/*.c)
TEST_OBJS := $(patsubst %.c,build/test/%.o,$(TEST_SRCS))

# -Itests lets cases under tests/core/ and tests/drivers/ pull in the shared
# harness (test.h) and mock transport (mock_hid.h) that live in tests/
build/test/%.o: %.c build/default_art.h $(ALL_ART_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Itests $(CFLAGS) -c -o $@ $<

build/test/run-tests: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

test: build/test/run-tests
	./build/test/run-tests

list-drivers:
	@printf '%s\n' $(ALL_DRIVERS)

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

# Install the binary and the udev rule that grants the pointer-transform
# daemon access to /dev/uinput.
# Autostart entries are created at runtime (per device, when the user enables the engine),
# so nothing is installed for them here.
# Reload udev afterwards; the message below says how.
install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	install -Dm644 dist/udev/70-alloyctl-uinput.rules \
		$(DESTDIR)$(UDEVDIR)/70-alloyctl-uinput.rules
	@echo
	@echo "Installed $(BIN) to $(DESTDIR)$(BINDIR) and the uinput udev rule."
	@echo "Activate the rule:  sudo udevadm control --reload && sudo udevadm trigger"
	@echo "For /dev/input and /dev/uinput access on non-logind systems, add"
	@echo "your user to the 'input' group:  sudo usermod -aG input \$$USER"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	rm -f $(DESTDIR)$(UDEVDIR)/70-alloyctl-uinput.rules

clean:
	rm -rf build $(BIN)
	$(MAKE) -C Documentation clean

-include $(DEPS) $(TEST_OBJS:.o=.d)

.PHONY: all install uninstall test test-asan test-ubsan test-tsan test-valgrind \
	check-format format htmldocs checkdocs docs-serve check-patch \
	codeowners check-codeowners check-version-tag clean list-drivers
