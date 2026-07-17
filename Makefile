# SPDX-License-Identifier: GPL-2.0-only

CC ?= gcc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -Wextra -Wshadow -Wmissing-prototypes \
	  -Wstrict-prototypes -MMD -MP
CFLAGS += $(shell $(PKG_CONFIG) --cflags ncursesw)
CPPFLAGS += -Isrc -Ibuild
LDLIBS += $(shell $(PKG_CONFIG) --libs ncursesw)

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

# Unit tests build without ncurses and with mocked HID transport
TEST_SRCS := $(wildcard tests/*.c) src/driver.c $(wildcard drivers/*.c)
TEST_OBJS := $(patsubst %.c,build/test/%.o,$(TEST_SRCS))

build/test/%.o: %.c build/default_art.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/test/run-tests: $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJS)

test: build/test/run-tests
	./build/test/run-tests

check-format:
	@fail=0; \
	for f in $$(git ls-files '*.c' '*.h'); do \
		if ! clang-format --dry-run -Werror $$f; then fail=1; fi; \
	done; \
	exit $$fail

format:
	clang-format -i $$(git ls-files '*.c' '*.h')

clean:
	rm -rf build $(BIN)

-include $(DEPS)

.PHONY: all test check-format format clean
