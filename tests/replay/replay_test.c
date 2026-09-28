#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
   SCREEN_ROWS = 4,
   SCREEN_COLS = 20,
   CSI_CAPACITY = 64,
   SNAPSHOT_CAPACITY = 512
};

enum mode {
   MODE_NORMAL,
   MODE_INSERT
};

enum parser_state {
   PARSER_GROUND,
   PARSER_ESCAPE,
   PARSER_CSI,
   PARSER_PASTE
};

struct screen {
   uint32_t cells[SCREEN_ROWS][SCREEN_COLS];
   size_t lengths[SCREEN_ROWS];
   size_t row;
   size_t column;
   enum mode mode;
};

struct parser {
   struct screen screen;
   enum parser_state state;
   char csi[CSI_CAPACITY];
   size_t csi_length;
   size_t paste_end_match;
   int failed;
};

struct file_contents {
   unsigned char *data;
   size_t length;
};

static void
screen_insert(struct screen *screen, uint32_t codepoint)
{
   size_t index;

   if (codepoint == (uint32_t)'\n') {
      if (screen->row + 1U < SCREEN_ROWS) {
         screen->row++;
         screen->column = 0U;
      }
      return;
   }

   if (screen->column >= SCREEN_COLS || screen->lengths[screen->row] >= SCREEN_COLS) {
      return;
   }

   for (index = screen->lengths[screen->row]; index > screen->column; index--) {
      screen->cells[screen->row][index] = screen->cells[screen->row][index - 1U];
   }
   screen->cells[screen->row][screen->column] = codepoint;
   screen->column++;
   screen->lengths[screen->row]++;
}

static void
screen_delete(struct screen *screen)
{
   size_t index;

   if (screen->column >= screen->lengths[screen->row]) {
      return;
   }
   for (index = screen->column; index + 1U < screen->lengths[screen->row]; index++) {
      screen->cells[screen->row][index] = screen->cells[screen->row][index + 1U];
   }
   screen->lengths[screen->row]--;
   screen->cells[screen->row][screen->lengths[screen->row]] = 0U;
}

static void
handle_key(struct parser *parser, uint32_t key, uint32_t text)
{
   struct screen *screen = &parser->screen;

   if (screen->mode == MODE_INSERT) {
      if (key == 27U) {
         screen->mode = MODE_NORMAL;
         if (screen->column > 0U) {
            screen->column--;
         }
      } else if (key == 13U) {
         screen_insert(screen, (uint32_t)'\n');
      } else if (key == 127U) {
         if (screen->column > 0U) {
            screen->column--;
            screen_delete(screen);
         }
      } else if (text != 0U) {
         screen_insert(screen, text);
      }
      return;
   }

   if (key == (uint32_t)'i') {
      screen->mode = MODE_INSERT;
   } else if (key == (uint32_t)'h') {
      if (screen->column > 0U) {
         screen->column--;
      }
   } else if (key == (uint32_t)'x') {
      screen_delete(screen);
   }
}

static int
parse_decimal(const char *begin, const char *end, uint32_t *value)
{
   uint32_t parsed = 0U;
   const char *cursor;

   if (begin == end) {
      return 0;
   }
   for (cursor = begin; cursor < end; cursor++) {
      uint32_t digit;

      if (*cursor < '0' || *cursor > '9') {
         return 0;
      }
      digit = (uint32_t)(unsigned int)(*cursor - '0');
      if (parsed > (UINT32_MAX - digit) / 10U) {
         return 0;
      }
      parsed = (parsed * 10U) + digit;
   }
   *value = parsed;
   return 1;
}

static int
parse_key_event(const char *body, size_t length, uint32_t *key, uint32_t *text)
{
   const char *end = body + length;
   const char *first_separator = memchr(body, ';', length);
   const char *key_end;
   const char *second_separator;
   const char *alternate;

   key_end = memchr(body, ':', length);
   if (key_end == NULL || (first_separator != NULL && key_end > first_separator)) {
      key_end = first_separator == NULL ? end : first_separator;
   }
   if (!parse_decimal(body, key_end, key)) {
      return 0;
   }

   *text = 0U;
   if (first_separator == NULL) {
      return 1;
   }
   second_separator = memchr(first_separator + 1, ';',
      (size_t)(end - (first_separator + 1)));
   if (second_separator == NULL) {
      return 1;
   }
   alternate = second_separator + 1;
   if (alternate == end) {
      return 1;
   }
   return parse_decimal(alternate, end, text);
}

static void
finish_csi(struct parser *parser, unsigned char final)
{
   uint32_t key;
   uint32_t text;

   if (final == (unsigned char)'~' && parser->csi_length == 3U &&
       memcmp(parser->csi, "200", 3U) == 0) {
      if (parser->screen.mode != MODE_INSERT) {
         parser->failed = 1;
         return;
      }
      parser->state = PARSER_PASTE;
      return;
   }
   if (final != (unsigned char)'u' ||
       !parse_key_event(parser->csi, parser->csi_length, &key, &text)) {
      parser->failed = 1;
      return;
   }
   handle_key(parser, key, text);
   parser->state = PARSER_GROUND;
}

static void
feed_paste_byte(struct parser *parser, unsigned char byte)
{
   static const unsigned char end_marker[] = "\x1b[201~";
   size_t index;

   if (byte == end_marker[parser->paste_end_match]) {
      parser->paste_end_match++;
      if (parser->paste_end_match == sizeof(end_marker) - 1U) {
         parser->paste_end_match = 0U;
         parser->state = PARSER_GROUND;
      }
      return;
   }

   for (index = 0U; index < parser->paste_end_match; index++) {
      screen_insert(&parser->screen, (uint32_t)end_marker[index]);
   }
   parser->paste_end_match = 0U;
   if (byte == end_marker[0]) {
      parser->paste_end_match = 1U;
   } else {
      screen_insert(&parser->screen, (uint32_t)byte);
   }
}

static void
parser_feed(struct parser *parser, unsigned char byte)
{
   if (parser->failed != 0) {
      return;
   }
   if (parser->state == PARSER_PASTE) {
      feed_paste_byte(parser, byte);
      return;
   }

   switch (parser->state) {
   case PARSER_GROUND:
      if (byte == 0x1bU) {
         parser->state = PARSER_ESCAPE;
      } else {
         parser->failed = 1;
      }
      break;
   case PARSER_ESCAPE:
      if (byte == (unsigned char)'[') {
         parser->state = PARSER_CSI;
         parser->csi_length = 0U;
      } else {
         parser->failed = 1;
      }
      break;
   case PARSER_CSI:
      if (byte >= 0x40U && byte <= 0x7eU) {
         finish_csi(parser, byte);
      } else if (parser->csi_length < sizeof(parser->csi)) {
         parser->csi[parser->csi_length] = (char)byte;
         parser->csi_length++;
      } else {
         parser->failed = 1;
      }
      break;
   case PARSER_PASTE:
      break;
   }
}

static int
append_byte(char *output, size_t capacity, size_t *length, unsigned char byte)
{
   if (*length >= capacity) {
      return 0;
   }
   output[*length] = (char)byte;
   (*length)++;
   return 1;
}

static int
append_codepoint(char *output, size_t capacity, size_t *length, uint32_t codepoint)
{
   if (codepoint <= 0x7fU) {
      return append_byte(output, capacity, length, (unsigned char)codepoint);
   }
   if (codepoint <= 0x7ffU) {
      return append_byte(output, capacity, length,
         (unsigned char)(0xc0U | (codepoint >> 6U))) &&
         append_byte(output, capacity, length,
         (unsigned char)(0x80U | (codepoint & 0x3fU)));
   }
   if (codepoint <= 0xffffU) {
      return append_byte(output, capacity, length,
         (unsigned char)(0xe0U | (codepoint >> 12U))) &&
         append_byte(output, capacity, length,
         (unsigned char)(0x80U | ((codepoint >> 6U) & 0x3fU))) &&
         append_byte(output, capacity, length,
         (unsigned char)(0x80U | (codepoint & 0x3fU)));
   }
   return 0;
}

static int
render_snapshot(const struct screen *screen, char *output, size_t capacity,
   size_t *output_length)
{
   size_t length = 0U;
   size_t row;
   size_t column;
   char status[SCREEN_COLS + 1U];
   int status_length;

   if (!append_byte(output, capacity, &length, (unsigned char)'+')) {
      return 0;
   }
   for (column = 0U; column < SCREEN_COLS; column++) {
      if (!append_byte(output, capacity, &length, (unsigned char)'-')) {
         return 0;
      }
   }
   if (!append_byte(output, capacity, &length, (unsigned char)'+') ||
       !append_byte(output, capacity, &length, (unsigned char)'\n')) {
      return 0;
   }

   for (row = 0U; row < SCREEN_ROWS; row++) {
      if (!append_byte(output, capacity, &length, (unsigned char)'|')) {
         return 0;
      }
      for (column = 0U; column < SCREEN_COLS; column++) {
         uint32_t codepoint = column < screen->lengths[row]
            ? screen->cells[row][column] : (uint32_t)' ';
         if (!append_codepoint(output, capacity, &length, codepoint)) {
            return 0;
         }
      }
      if (!append_byte(output, capacity, &length, (unsigned char)'|') ||
          !append_byte(output, capacity, &length, (unsigned char)'\n')) {
         return 0;
      }
   }

   if (!append_byte(output, capacity, &length, (unsigned char)'+')) {
      return 0;
   }
   for (column = 0U; column < SCREEN_COLS; column++) {
      if (!append_byte(output, capacity, &length, (unsigned char)'-')) {
         return 0;
      }
   }
   if (!append_byte(output, capacity, &length, (unsigned char)'+') ||
       !append_byte(output, capacity, &length, (unsigned char)'\n')) {
      return 0;
   }

   status_length = snprintf(status, sizeof(status), "%s  %zu:%zu",
      screen->mode == MODE_NORMAL ? "NORMAL" : "INSERT",
      screen->row + 1U, screen->column + 1U);
   if (status_length < 0 || (size_t)status_length > SCREEN_COLS) {
      return 0;
   }
   if (!append_byte(output, capacity, &length, (unsigned char)'|')) {
      return 0;
   }
   for (column = 0U; column < SCREEN_COLS; column++) {
      unsigned char byte = column < (size_t)status_length
         ? (unsigned char)status[column] : (unsigned char)' ';
      if (!append_byte(output, capacity, &length, byte)) {
         return 0;
      }
   }
   if (!append_byte(output, capacity, &length, (unsigned char)'|') ||
       !append_byte(output, capacity, &length, (unsigned char)'\n')) {
      return 0;
   }

   *output_length = length;
   return 1;
}

static int
read_file(const char *path, struct file_contents *contents)
{
   FILE *file;
   long file_length;
   size_t length;

   file = fopen(path, "rb");
   if (file == NULL) {
      fprintf(stderr, "%s: %s\n", path, strerror(errno));
      return 0;
   }
   if (fseek(file, 0L, SEEK_END) != 0 || (file_length = ftell(file)) < 0L ||
       fseek(file, 0L, SEEK_SET) != 0) {
      fprintf(stderr, "%s: cannot determine file length\n", path);
      (void)fclose(file);
      return 0;
   }
   length = (size_t)file_length;
   contents->data = malloc(length == 0U ? 1U : length);
   if (contents->data == NULL) {
      fprintf(stderr, "%s: out of memory\n", path);
      (void)fclose(file);
      return 0;
   }
   contents->length = length;
   if (fread(contents->data, 1U, length, file) != length) {
      fprintf(stderr, "%s: read failed\n", path);
      free(contents->data);
      contents->data = NULL;
      (void)fclose(file);
      return 0;
   }
   if (fclose(file) != 0) {
      fprintf(stderr, "%s: close failed\n", path);
      free(contents->data);
      contents->data = NULL;
      return 0;
   }
   return 1;
}

int
main(int argc, char **argv)
{
   struct file_contents recording = {NULL, 0U};
   struct file_contents expected = {NULL, 0U};
   struct parser parser;
   char actual[SNAPSHOT_CAPACITY];
   size_t actual_length = 0U;
   size_t index;
   int result = EXIT_FAILURE;

   if (argc != 3) {
      fprintf(stderr, "usage: %s RAW_RECORDING EXPECTED_SNAPSHOT\n", argv[0]);
      return EXIT_FAILURE;
   }
   if (!read_file(argv[1], &recording) || !read_file(argv[2], &expected)) {
      goto done;
   }

   (void)memset(&parser, 0, sizeof(parser));
   parser.screen.mode = MODE_NORMAL;
   for (index = 0U; index < recording.length; index++) {
      parser_feed(&parser, recording.data[index]);
   }
   if (parser.failed != 0 || parser.state != PARSER_GROUND) {
      fprintf(stderr, "raw recording ended with an invalid or incomplete input sequence\n");
      goto done;
   }
   if (!render_snapshot(&parser.screen, actual, sizeof(actual), &actual_length)) {
      fprintf(stderr, "screen snapshot exceeded the fixed output buffer\n");
      goto done;
   }
   if (actual_length != expected.length ||
       memcmp(actual, expected.data, actual_length) != 0) {
      fprintf(stderr, "snapshot mismatch: %s\n--- expected ---\n", argv[2]);
      (void)fwrite(expected.data, 1U, expected.length, stderr);
      (void)fputs("--- actual ---\n", stderr);
      (void)fwrite(actual, 1U, actual_length, stderr);
      goto done;
   }

   printf("replay snapshot matches %s\n", argv[2]);
   result = EXIT_SUCCESS;

done:
   free(expected.data);
   free(recording.data);
   return result;
}
