.POSIX:

CC = cc
CLANG_TIDY = clang-tidy
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
LDFLAGS =

BUILD_DIR = build
SMOKE = $(BUILD_DIR)/cwiki-smoke
REPLAY_TEST = $(BUILD_DIR)/cwiki-replay-test
FIXTURE_VAULT_TEST = $(BUILD_DIR)/test-fixture-vault
REFERENCE_BUFFER_TEST = $(BUILD_DIR)/test-reference-buffer
SUPPORT_TESTS = $(FIXTURE_VAULT_TEST) $(REFERENCE_BUFFER_TEST)
ANALYZE_SOURCES = tests/smoke.c tests/replay/replay_test.c \
	tests/support/fixture_vault.c tests/support/reference_buffer.c \
	tests/support/test_fixture_vault.c tests/support/test_reference_buffer.c

.PHONY: all smoke replay-test support-test test check sanitize analyze verify \
	demo clean

all: smoke

smoke: $(SMOKE)

$(SMOKE): tests/smoke.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/smoke.c $(LDFLAGS) -o $(SMOKE)

$(REPLAY_TEST): tests/replay/replay_test.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/replay/replay_test.c $(LDFLAGS) \
		-o $(REPLAY_TEST)

replay-test: $(REPLAY_TEST)
	$(REPLAY_TEST) tests/fixtures/keys/seed.keys.raw \
		tests/snapshots/seed.screen

$(FIXTURE_VAULT_TEST): tests/support/fixture_vault.c \
		tests/support/fixture_vault.h tests/support/test_fixture_vault.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/support/fixture_vault.c \
		tests/support/test_fixture_vault.c $(LDFLAGS) -o $(FIXTURE_VAULT_TEST)

$(REFERENCE_BUFFER_TEST): tests/support/reference_buffer.c \
		tests/support/reference_buffer.h tests/support/test_reference_buffer.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/support/reference_buffer.c \
		tests/support/test_reference_buffer.c $(LDFLAGS) \
		-o $(REFERENCE_BUFFER_TEST)

support-test: $(SUPPORT_TESTS)
	$(FIXTURE_VAULT_TEST)
	$(REFERENCE_BUFFER_TEST)

test: smoke replay-test support-test
	$(SMOKE)

check: test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined" \
		LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" test

analyze:
	$(CLANG_TIDY) $(ANALYZE_SOURCES) -- $(CPPFLAGS) $(CFLAGS)

verify: check sanitize

demo: replay-test

clean:
	rm -rf $(BUILD_DIR)
