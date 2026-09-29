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
KEY_RECORD_TEST = $(BUILD_DIR)/test-key-record
BUFFER_TEST = $(BUILD_DIR)/test-buffer
CAPABILITIES_TEST = $(BUILD_DIR)/test-capabilities
TERMINAL_TEST = $(BUILD_DIR)/test-terminal
UNDO_TEST = $(BUILD_DIR)/test-undo
ZONE_TEST = $(BUILD_DIR)/test-zone
ZONE_FUZZ = $(BUILD_DIR)/fuzz-zone
REGEX_TEST = $(BUILD_DIR)/test-regex
VIM_REGEX_TEST = $(BUILD_DIR)/test-vim-regex
VIM_REGEX_FUZZ = $(BUILD_DIR)/fuzz-vim-regex
CONCEAL_TEST = $(BUILD_DIR)/test-conceal
LAYOUT_TEST = $(BUILD_DIR)/test-layout
SNIPPET_TEST = $(BUILD_DIR)/test-snippet
SNIPPET_FUZZ = $(BUILD_DIR)/fuzz-snippet
ACTION_TEST = $(BUILD_DIR)/test-action
STRUCTURAL_SEARCH_TEST = $(BUILD_DIR)/test-structural-search
DOCUMENT_TEST = $(BUILD_DIR)/test-document
KEYMAP_TEST = $(BUILD_DIR)/test-keymap
MOTION_TEST = $(BUILD_DIR)/test-motion
EDITOR_TEST = $(BUILD_DIR)/test-editor
EDITOR_INPUT_TEST = $(BUILD_DIR)/test-editor-input
HIGHLIGHT_TEST = $(BUILD_DIR)/test-highlight
SUPPORT_TESTS = $(FIXTURE_VAULT_TEST) $(REFERENCE_BUFFER_TEST)
PRODUCT_TESTS = $(DURABLE_WRITE_TEST) $(INPUT_TEST) $(KEY_RECORD_TEST) $(BUFFER_TEST) \
	$(CAPABILITIES_TEST) $(TERMINAL_TEST) $(UNDO_TEST) $(ZONE_TEST) \
	$(ZONE_FUZZ) $(REGEX_TEST) $(VIM_REGEX_TEST) $(VIM_REGEX_FUZZ) \
	$(CONCEAL_TEST) $(LAYOUT_TEST) $(SNIPPET_TEST) $(SNIPPET_FUZZ) \
	$(ACTION_TEST) $(STRUCTURAL_SEARCH_TEST) $(DOCUMENT_TEST) $(KEYMAP_TEST) \
	$(MOTION_TEST) $(EDITOR_TEST) $(EDITOR_INPUT_TEST) $(HIGHLIGHT_TEST)
ANALYZE_SOURCES = tests/smoke.c tests/replay/replay_test.c \
	tests/support/fixture_vault.c tests/support/reference_buffer.c \
	tests/support/test_fixture_vault.c tests/support/test_reference_buffer.c \
	src/durable_write.c tests/io/test_durable_write.c \
	src/input.c tests/input/input_test.c src/key_record.c \
	tests/input/key_record_test.c src/buffer.c src/unicode.c \
	tests/buffer/test_buffer.c src/capabilities.c \
	tests/terminal/capabilities_test.c src/terminal.c \
	tests/terminal/terminal_test.c src/undo.c tests/undo/test_undo.c \
	src/zone.c tests/zone/test_zone.c tests/zone/fuzz_zone.c src/regex.c \
	tests/regex/test_regex.c src/vim_regex.c tests/regex/test_vim_regex.c \
	tests/regex/fuzz_vim_regex.c src/conceal.c tests/conceal/test_conceal.c \
	src/layout.c tests/layout/test_layout.c src/snippet.c \
	tests/snippet/test_snippet.c tests/snippet/fuzz_snippet.c src/action.c \
	tests/action/test_action.c src/structural_search.c \
	tests/zone/test_structural_search.c src/document.c tests/io/test_document.c \
	src/keymap.c tests/action/test_keymap.c src/motion.c \
	tests/editor/test_motion.c src/editor.c tests/editor/test_editor.c \
	src/editor_input.c tests/editor/test_editor_input.c src/highlight.c \
	tests/highlight/test_highlight.c

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

$(KEY_RECORD_TEST): src/key_record.c src/key_record.h src/input.c src/input.h \
		tests/input/key_record_test.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCWIKI_KEY_RECORD_TESTING -Isrc \
		src/input.c src/key_record.c tests/input/key_record_test.c $(LDFLAGS) \
		-o $(KEY_RECORD_TEST)

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

$(TERMINAL_TEST): src/terminal.c src/terminal.h src/capabilities.c \
		src/capabilities.h src/input.c src/input.h src/key_record.c \
		src/key_record.h tests/terminal/terminal_test.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCWIKI_TERMINAL_TESTING \
		-DCWIKI_KEY_RECORD_TESTING -Isrc src/capabilities.c src/input.c \
		src/key_record.c src/terminal.c tests/terminal/terminal_test.c $(LDFLAGS) \
		-o $(TERMINAL_TEST)

$(UNDO_TEST): src/undo.c src/undo.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h tests/undo/test_undo.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) -DCWIKI_UNDO_TESTING \
		-Isrc src/buffer.c src/unicode.c src/undo.c tests/undo/test_undo.c \
		$(LDFLAGS) $(UTF8PROC_LIBS) -o $(UNDO_TEST)

$(ZONE_TEST): src/zone.c src/zone.h src/regex.c src/regex.h src/buffer.c \
		src/buffer.h src/unicode.c src/unicode.h tests/zone/test_zone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/zone.c \
		tests/zone/test_zone.c $(LDFLAGS) $(UTF8PROC_LIBS) $(PCRE2_LIBS) \
		-o $(ZONE_TEST)

$(ZONE_FUZZ): src/zone.c src/zone.h src/regex.c src/regex.h src/buffer.c \
		src/buffer.h src/unicode.c src/unicode.h tests/zone/fuzz_zone.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) \
		-DCWIKI_ZONE_FUZZ_STANDALONE -Isrc src/buffer.c src/unicode.c \
		src/regex.c src/zone.c tests/zone/fuzz_zone.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(ZONE_FUZZ)

$(REGEX_TEST): src/regex.c src/regex.h tests/regex/test_regex.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PCRE2_CFLAGS) -Isrc src/regex.c \
		tests/regex/test_regex.c $(LDFLAGS) $(PCRE2_LIBS) -o $(REGEX_TEST)

$(VIM_REGEX_TEST): src/vim_regex.c src/vim_regex.h src/regex.c src/regex.h \
		tests/regex/test_vim_regex.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/vim_regex.c src/regex.c tests/regex/test_vim_regex.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(VIM_REGEX_TEST)

$(VIM_REGEX_FUZZ): src/vim_regex.c src/vim_regex.h src/regex.c src/regex.h \
		tests/regex/fuzz_vim_regex.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) \
		-DCWIKI_VIM_REGEX_FUZZ_STANDALONE -Isrc src/vim_regex.c src/regex.c \
		tests/regex/fuzz_vim_regex.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(VIM_REGEX_FUZZ)

$(CONCEAL_TEST): src/conceal.c src/conceal.h src/buffer.c src/buffer.h \
		src/unicode.c src/unicode.h src/zone.c src/zone.h src/regex.c \
		src/regex.h tests/conceal/test_conceal.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/zone.c src/conceal.c \
		tests/conceal/test_conceal.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(CONCEAL_TEST)

$(LAYOUT_TEST): src/layout.c src/layout.h src/conceal.c src/conceal.h \
		src/buffer.c src/buffer.h src/unicode.c src/unicode.h src/zone.c \
		src/zone.h src/regex.c src/regex.h tests/layout/test_layout.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/zone.c src/conceal.c \
		src/layout.c tests/layout/test_layout.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(LAYOUT_TEST)

$(SNIPPET_TEST): src/snippet.c src/snippet.h src/buffer.c src/buffer.h \
		src/unicode.c src/unicode.h src/undo.c src/undo.h src/regex.c \
		src/regex.h src/zone.c src/zone.h tests/snippet/test_snippet.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) \
		-DCWIKI_SNIPPET_TESTING -DCWIKI_UNDO_TESTING -Isrc src/buffer.c \
		src/unicode.c src/undo.c src/regex.c src/zone.c src/snippet.c \
		tests/snippet/test_snippet.c $(LDFLAGS) $(UTF8PROC_LIBS) $(PCRE2_LIBS) \
		-o $(SNIPPET_TEST)

$(SNIPPET_FUZZ): src/snippet.c src/snippet.h src/buffer.c src/buffer.h \
		src/unicode.c src/unicode.h src/undo.c src/undo.h src/regex.c \
		src/regex.h src/zone.c src/zone.h tests/snippet/fuzz_snippet.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) \
		-DCWIKI_SNIPPET_TESTING -DCWIKI_SNIPPET_FUZZ_STANDALONE -Isrc \
		src/buffer.c src/unicode.c src/undo.c src/regex.c src/zone.c \
		src/snippet.c tests/snippet/fuzz_snippet.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(SNIPPET_FUZZ)

$(ACTION_TEST): src/action.c src/action.h tests/action/test_action.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCWIKI_ACTION_TESTING -Isrc src/action.c \
		tests/action/test_action.c $(LDFLAGS) -o $(ACTION_TEST)

$(STRUCTURAL_SEARCH_TEST): src/structural_search.c src/structural_search.h \
		src/regex.c src/regex.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h tests/zone/test_structural_search.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/structural_search.c \
		tests/zone/test_structural_search.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(STRUCTURAL_SEARCH_TEST)

$(DOCUMENT_TEST): src/document.c src/document.h src/durable_write.c \
		src/durable_write.h src/buffer.c src/buffer.h src/unicode.c src/unicode.h \
		tests/io/test_document.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) \
		-DCWIKI_DOCUMENT_TESTING -DCWIKI_DURABLE_WRITE_TESTING -Isrc \
		src/buffer.c src/unicode.c src/durable_write.c src/document.c \
		tests/io/test_document.c $(LDFLAGS) $(UTF8PROC_LIBS) -o $(DOCUMENT_TEST)

$(KEYMAP_TEST): src/keymap.c src/keymap.h src/action.c src/action.h \
		src/input.h tests/action/test_keymap.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCWIKI_KEYMAP_TESTING -DCWIKI_ACTION_TESTING \
		-Isrc src/action.c src/keymap.c tests/action/test_keymap.c $(LDFLAGS) \
		-o $(KEYMAP_TEST)

$(MOTION_TEST): src/motion.c src/motion.h src/layout.c src/layout.h \
		src/conceal.c src/conceal.h src/buffer.c src/buffer.h src/unicode.c \
		src/unicode.h src/zone.c src/zone.h src/regex.c src/regex.h \
		tests/editor/test_motion.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/zone.c src/conceal.c \
		src/layout.c src/motion.c tests/editor/test_motion.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(MOTION_TEST)

$(EDITOR_TEST): src/editor.c src/editor.h src/document.c src/document.h \
		src/durable_write.c src/durable_write.h src/motion.c src/motion.h \
		src/layout.c src/layout.h src/conceal.c src/conceal.h src/undo.c \
		src/undo.h src/buffer.c src/buffer.h src/unicode.c src/unicode.h \
		src/zone.c src/zone.h src/regex.c src/regex.h \
		tests/editor/test_editor.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/undo.c src/regex.c src/zone.c \
		src/conceal.c src/layout.c src/motion.c src/durable_write.c \
		src/document.c src/editor.c tests/editor/test_editor.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(EDITOR_TEST)

$(EDITOR_INPUT_TEST): src/editor_input.c src/editor_input.h src/editor.c \
		src/editor.h src/action.c src/action.h src/keymap.c src/keymap.h \
		src/input.h src/document.c src/document.h src/durable_write.c \
		src/durable_write.h src/motion.c src/motion.h src/layout.c src/layout.h \
		src/conceal.c src/conceal.h src/undo.c src/undo.h src/buffer.c \
		src/buffer.h src/unicode.c src/unicode.h src/zone.c src/zone.h \
		src/regex.c src/regex.h tests/editor/test_editor_input.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/undo.c src/regex.c src/zone.c \
		src/conceal.c src/layout.c src/motion.c src/durable_write.c \
		src/document.c src/editor.c src/action.c src/keymap.c \
		src/editor_input.c tests/editor/test_editor_input.c $(LDFLAGS) \
		$(UTF8PROC_LIBS) $(PCRE2_LIBS) -o $(EDITOR_INPUT_TEST)

$(HIGHLIGHT_TEST): src/highlight.c src/highlight.h src/buffer.c src/buffer.h \
		src/unicode.c src/unicode.h src/zone.c src/zone.h src/regex.c \
		src/regex.h tests/highlight/test_highlight.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc \
		src/buffer.c src/unicode.c src/regex.c src/zone.c src/highlight.c \
		tests/highlight/test_highlight.c $(LDFLAGS) $(UTF8PROC_LIBS) \
		$(PCRE2_LIBS) -o $(HIGHLIGHT_TEST)

product-test: $(PRODUCT_TESTS)
	$(DURABLE_WRITE_TEST)
	$(INPUT_TEST)
	$(KEY_RECORD_TEST)
	$(BUFFER_TEST)
	$(CAPABILITIES_TEST)
	$(TERMINAL_TEST)
	$(UNDO_TEST)
	$(ZONE_TEST)
	$(ZONE_FUZZ)
	$(REGEX_TEST)
	$(VIM_REGEX_TEST)
	$(VIM_REGEX_FUZZ)
	$(CONCEAL_TEST)
	$(LAYOUT_TEST)
	$(SNIPPET_TEST)
	$(SNIPPET_FUZZ)
	$(ACTION_TEST)
	$(STRUCTURAL_SEARCH_TEST)
	$(DOCUMENT_TEST)
	$(KEYMAP_TEST)
	$(MOTION_TEST)
	$(EDITOR_TEST)
	$(EDITOR_INPUT_TEST)
	$(HIGHLIGHT_TEST)

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
		-DCWIKI_DURABLE_WRITE_TESTING -DCWIKI_DOCUMENT_TESTING \
		-DCWIKI_TERMINAL_TESTING \
		-DCWIKI_KEY_RECORD_TESTING \
		-DCWIKI_UNDO_TESTING -DCWIKI_SNIPPET_TESTING \
		-DCWIKI_ACTION_TESTING -DCWIKI_KEYMAP_TESTING \
		-DCWIKI_ZONE_FUZZ_STANDALONE -DCWIKI_VIM_REGEX_FUZZ_STANDALONE \
		-DCWIKI_SNIPPET_FUZZ_STANDALONE \
		$(UTF8PROC_CFLAGS) $(PCRE2_CFLAGS) -Isrc

verify: check sanitize

demo: replay-test

clean:
	rm -rf $(BUILD_DIR)
