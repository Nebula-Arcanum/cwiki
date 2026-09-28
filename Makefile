.POSIX:

CC = cc
CLANG_TIDY = clang-tidy
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
LDFLAGS =

BUILD_DIR = build
SMOKE = $(BUILD_DIR)/cwiki-smoke

.PHONY: all smoke test check sanitize analyze verify demo clean

all: smoke

smoke: $(SMOKE)

$(SMOKE): tests/smoke.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/smoke.c $(LDFLAGS) -o $(SMOKE)

test: smoke
	$(SMOKE)

check: test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined" \
		LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" test

analyze:
	$(CLANG_TIDY) tests/smoke.c -- $(CPPFLAGS) $(CFLAGS)

verify: check sanitize

demo: check

clean:
	rm -rf $(BUILD_DIR)
