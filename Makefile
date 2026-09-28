.POSIX:

CC = cc
CLANG_TIDY = clang-tidy
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700
CFLAGS = -std=c11 -pedantic -Wall -Wextra -Werror -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes
LDFLAGS =
UTF8PROC_CFLAGS = `pkg-config --cflags libutf8proc`
UTF8PROC_LIBS = `pkg-config --libs libutf8proc`
PCRE2_CFLAGS = `pkg-config --cflags libpcre2-8`
PCRE2_LIBS = `pkg-config --libs libpcre2-8`

BUILD_DIR = build
SMOKE = $(BUILD_DIR)/cwiki-smoke
REPLAY_TEST = $(BUILD_DIR)/cwiki-replay-test
FIXTURE_VAULT_TEST = $(BUILD_DIR)/test-fixture-vault
REFERENCE_BUFFER_TEST = $(BUILD_DIR)/test-reference-buffer
DURABLE_WRITE_TEST = $(BUILD_DIR)/test-durable-write
INPUT_TEST = $(BUILD_DIR)/test-input
BUFFER_TEST = $(BUILD_DIR)/test-buffer
CAPABILITIES_TEST = $(BUILD_DIR)/test-capabilities
UNDO_TEST = $(BUILD_DIR)/test-undo
ZONE_TEST = $(BUILD_DIR)/test-zone
ZONE_FUZZ = $(BUILD_DIR)/fuzz-zone
REGEX_TEST = $(BUILD_DIR)/test-regex
SUPPORT_TESTS = $(FIXTURE_VAULT_TEST) $(REFERENCE_BUFFER_TEST)
PRODUCT_TESTS = $(DURABLE_WRITE_TEST) $(INPUT_TEST) $(BUFFER_TEST) \
	$(CAPABILITIES_TEST) $(UNDO_TEST) $(ZONE_TEST) $(ZONE_FUZZ) $(REGEX_TEST)
ANALYZE_SOURCES = tests/smoke.c tests/replay/replay_test.c \
	tests/support/fixture_vault.c tests/support/reference_buffer.c \
	tests/support/test_fixture_vault.c tests/support/test_reference_buffer.c \
	src/durable_write.c tests/io/test_durable_write.c \
	src/input.c tests/input/input_test.c src/buffer.c src/unicode.c \
	tests/buffer/test_buffer.c src/capabilities.c \
	tests/terminal/capabilities_test.c src/undo.c tests/undo/test_undo.c \
	src/zone.c tests/zone/test_zone.c tests/zone/fuzz_zone.c src/regex.c \
	tests/regex/test_regex.c

.PHONY: all smoke replay-test support-test product-test test check sanitize \
	analyze verify demo clean

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

$(DURABLE_WRITE_TEST): src/durable_write.c src/durable_write.h \
		tests/io/test_durable_write.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCWIKI_DURABLE_WRITE_TESTING -Isrc \
		src/durable_write.c tests/io/test_durable_write.c $(LDFLAGS) \
		-o $(DURABLE_WRITE_TEST)

$(INPUT_TEST): src/input.c src/input.h tests/input/input_test.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc src/input.c tests/input/input_test.c \
		$(LDFLAGS) -o $(INPUT_TEST)

$(BUFFER_TEST): src/buffer.c src/buffer.h src/unicode.c src/unicode.h \
		tests/buffer/test_buffer.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) -Isrc src/buffer.c \
		src/unicode.c tests/buffer/test_buffer.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) -o $(BUFFER_TEST)

$(CAPABILITIES_TEST): src/capabilities.c src/capabilities.h \
		tests/terminal/capabilities_test.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc src/capabilities.c \
		tests/terminal/capabilities_test.c $(LDFLAGS) -o $(CAPABILITIES_TEST)

$(UNDO_TEST): src/undo.c src/undo.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h tests/undo/test_undo.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) -DCWIKI_UNDO_TESTING \
		-Isrc src/buffer.c src/unicode.c src/undo.c tests/undo/test_undo.c \
		$(LDFLAGS) $(UTF8PROC_LIBS) -o $(UNDO_TEST)

$(ZONE_TEST): src/zone.c src/zone.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h tests/zone/test_zone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/zone.c tests/zone/test_zone.c \
		$(LDFLAGS) $(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(ZONE_TEST)

$(ZONE_FUZZ): src/zone.c src/zone.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h tests/zone/fuzz_zone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) \
		-DCWIKI_ZONE_FUZZ_STANDALONE -Isrc src/buffer.c src/unicode.c \
		src/zone.c tests/zone/fuzz_zone.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(ZONE_FUZZ)

$(REGEX_TEST): src/regex.c src/regex.h tests/regex/test_regex.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCRE2_CFLAGS) -Isrc src/regex.c \
		tests/regex/test_regex.c $(LDFLAGS) $(PCRE2_LIBS) -o $(REGEX_TEST)

product-test: $(PRODUCT_TESTS)
	$(DURABLE_WRITE_TEST)
	$(INPUT_TEST)
	$(BUFFER_TEST)
	$(CAPABILITIES_TEST)
	$(UNDO_TEST)
	$(ZONE_TEST)
	$(ZONE_FUZZ)
	$(REGEX_TEST)

test: smoke replay-test support-test product-test
	$(SMOKE)

check: test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined" \
		LDFLAGS="$(LDFLAGS) -fsanitize=address,undefined" test

analyze:
	$(CLANG_TIDY) --checks='-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling' \
		--warnings-as-errors='*' \
		$(ANALYZE_SOURCES) -- $(CPPFLAGS) $(CFLAGS) \
		-DCWIKI_DURABLE_WRITE_TESTING -DCWIKI_UNDO_TESTING \
		-DCWIKI_ZONE_FUZZ_STANDALONE $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc

verify: check sanitize

demo: replay-test

clean:
	rm -rf $(BUILD_DIR)
