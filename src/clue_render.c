#include "clue_render.h"

#include "action.h"
#include "terminal.h"
#include "unicode.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <utf8proc.h>

struct output {
   char *bytes;
   size_t length;
   int error;
};

static void
emit(struct output *out, const char *bytes, size_t length)
{
   if (out->error != 0) {
      return;
   }
   if (length >= SIZE_MAX - out->length) {
      out->error = EOVERFLOW;
      return;
   }
   if (out->bytes != NULL) {
      (void)memcpy(out->bytes + out->length, bytes, length);
   }
   out->length += length;
}

static void
text(struct output *out, const char *bytes)
{
   emit(out, bytes, strlen(bytes));
}

static void
spaces(struct output *out, size_t count)
{
   while (count-- != 0U) {
      emit(out, " ", 1U);
   }
}

static void
position(struct output *out, size_t row, size_t column)
{
   char sequence[64];
   int length = snprintf(sequence, sizeof(sequence), "\x1b[%zu;%zuH",
       row, column);

   if (length < 0 || (size_t)length >= sizeof(sequence)) {
      out->error = EOVERFLOW;
      return;
   }
   emit(out, sequence, (size_t)length);
}

static size_t
paint(struct output *out, const char *bytes, size_t length, size_t width)
{
   size_t offset = 0U;
   size_t painted = 0U;

   while (offset < length && painted < width && out->error == 0) {
      size_t next = cwiki_grapheme_next(bytes, length, offset);
      int cells;

      if (next == SIZE_MAX) {
         out->error = EINVAL;
         return painted;
      }
      cells = cwiki_grapheme_width(bytes + offset, next - offset);
      if (cells < 0 || (size_t)cells > width - painted) {
         break;
      }
      if (cells > 0) {
         emit(out, bytes + offset, next - offset);
         painted += (size_t)cells;
      }
      offset = next;
   }
   return painted;
}

static void
repeat(struct output *out, const char *glyph, size_t count)
{
   while (count-- != 0U) {
      text(out, glyph);
   }
}

static void
border_row(struct output *out, const struct cwiki_float_area *area,
    size_t row, bool top, const char *title)
{
   size_t width = area->columns - 2U;
   size_t painted = 0U;

   position(out, row, area->left);
   text(out, "\x1b[0;90m");
   text(out, top ? "┌" : "└");
   if (top && title != NULL && title[0] != '\0' && width >= 3U) {
      text(out, "─ ");
      painted = 2U + paint(out, title, strlen(title), width - 2U);
      if (painted < width) {
         text(out, " ");
         painted++;
      }
   }
   repeat(out, "─", width - painted);
   text(out, top ? "┐" : "┘");
   text(out, "\x1b[0m");
}

static bool
append(char *bytes, size_t capacity, size_t *length, const char *text_value)
{
   size_t added = strlen(text_value);

   if (added >= capacity - *length) {
      return false;
   }
   (void)memcpy(bytes + *length, text_value, added);
   *length += added;
   bytes[*length] = '\0';
   return true;
}

static bool
append_codepoint(char *bytes, size_t capacity, size_t *length, uint32_t key)
{
   utf8proc_uint8_t encoded[4];
   utf8proc_ssize_t count;

   if (key > INT32_MAX) {
      return false;
   }
   count = utf8proc_encode_char((utf8proc_int32_t)key, encoded);
   if (count <= 0 || (size_t)count >= capacity - *length) {
      return false;
   }
   (void)memcpy(bytes + *length, encoded, (size_t)count);
   *length += (size_t)count;
   bytes[*length] = '\0';
   return true;
}

static bool
key_label(struct cwiki_key key, char *bytes, size_t capacity)
{
   static const struct {
      unsigned int modifier;
      const char *label;
   } modifiers[] = {
      {CWIKI_INPUT_CTRL, "C-"}, {CWIKI_INPUT_ALT, "A-"},
      {CWIKI_INPUT_SHIFT, "S-"}, {CWIKI_INPUT_SUPER, "Super-"},
      {CWIKI_INPUT_HYPER, "Hyper-"}, {CWIKI_INPUT_META, "Meta-"},
      {CWIKI_INPUT_CAPS_LOCK, "Caps-"}, {CWIKI_INPUT_NUM_LOCK, "Num-"}
   };
   const char *special = NULL;
   size_t length = 0U;
   size_t i;

   if (capacity == 0U) {
      return false;
   }
   bytes[0] = '\0';
   for (i = 0U; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
      if ((key.modifiers & modifiers[i].modifier) != 0U &&
          !append(bytes, capacity, &length, modifiers[i].label)) {
         return false;
      }
   }
   switch (key.key) {
   case 8U:
      special = "Backspace";
      break;
   case 9U:
      special = "Tab";
      break;
   case 13U:
      special = "Enter";
      break;
   case 27U:
      special = "Esc";
      break;
   case 32U:
      special = "Space";
      break;
   case 127U:
      special = "Delete";
      break;
   default:
      break;
   }
   if (special != NULL) {
      return append(bytes, capacity, &length, special);
   }
   if (key.key >= 0x20U && key.key != 0x7fU &&
       append_codepoint(bytes, capacity, &length, key.key)) {
      return true;
   }
   {
      char fallback[24];
      int written = snprintf(fallback, sizeof(fallback), "U+%04X", key.key);

      return written > 0 && (size_t)written < sizeof(fallback) &&
          append(bytes, capacity, &length, fallback);
   }
}

static void
content_row(struct output *out, const struct cwiki_keymap *keymap,
    const struct cwiki_action_registry *actions,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count, const struct cwiki_float_area *area, size_t row,
    size_t width, bool bordered, size_t index)
{
   struct cwiki_keymap_continuation clue;
   char key[96];
   const char *label = "";
   bool available = true;
   size_t painted = 0U;
   enum cwiki_keymap_status status;

   position(out, row, area->left);
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
   status = cwiki_keymap_continuation_at(keymap, mode, prefix, prefix_count,
       index, &clue);
   if (status != CWIKI_KEYMAP_OK || !key_label(clue.key, key, sizeof(key))) {
      out->error = EINVAL;
      return;
   }
   if (clue.completes) {
      label = clue.action.label;
      if (out->bytes != NULL &&
          cwiki_action_is_available(actions, clue.action.name, &available) !=
          CWIKI_ACTION_OK) {
         out->error = EINVAL;
         return;
      }
   } else if (clue.has_continuations) {
      label = "More…";
   }
   text(out, available ? "\x1b[0;36m" : "\x1b[0;90m");
   painted = paint(out, key, strlen(key), width);
   if (painted + 2U < width) {
      spaces(out, 2U);
      painted += 2U;
      text(out, available ? "\x1b[0;39m" : "\x1b[0;90m");
      painted += paint(out, label, strlen(label), width - painted);
      if (clue.completes && clue.has_continuations &&
          painted + 2U <= width) {
         text(out, " …");
         painted += 2U;
      }
   }
   spaces(out, width - painted);
   text(out, "\x1b[0m");
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
}

static void
empty_row(struct output *out, const struct cwiki_float_area *area, size_t row,
    size_t width, bool bordered)
{
   static const char message[] = "No continuations";
   size_t painted;

   position(out, row, area->left);
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
   text(out, "\x1b[0;90m");
   painted = paint(out, message, sizeof(message) - 1U, width);
   spaces(out, width - painted);
   text(out, "\x1b[0m");
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
}

static void
blank_row(struct output *out, const struct cwiki_float_area *area, size_t row,
    size_t width, bool bordered)
{
   position(out, row, area->left);
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
   spaces(out, width);
   if (bordered) {
      text(out, "\x1b[0;90m│\x1b[0m");
   }
}

static void
overlay(struct output *out, const struct cwiki_keymap *keymap,
    const struct cwiki_action_registry *actions,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count, const struct cwiki_float_area *area, bool bordered,
    const char *title)
{
   size_t inset = bordered ? 1U : 0U;
   size_t width = area->columns - inset * 2U;
   size_t rows = area->rows - inset * 2U;
   size_t count = cwiki_keymap_continuation_count(keymap, mode, prefix,
       prefix_count);
   size_t i;

   text(out, CWIKI_TERMINAL_CURSOR_HIDE);
   text(out, "\x1b[s");
   if (bordered) {
      border_row(out, area, area->top, true, title);
   }
   for (i = 0U; i < rows; i++) {
      size_t row = area->top + inset + i;

      if (count == 0U && i == 0U) {
         empty_row(out, area, row, width, bordered);
      } else if (i < count) {
         content_row(out, keymap, actions, mode, prefix, prefix_count, area,
             row, width, bordered, i);
      } else {
         blank_row(out, area, row, width, bordered);
      }
   }
   if (bordered) {
      border_row(out, area, area->top + area->rows - 1U, false, NULL);
   }
   text(out, "\x1b[u");
   text(out, CWIKI_TERMINAL_CURSOR_SHOW);
}

int
cwiki_clue_render_overlay(const struct cwiki_keymap *keymap,
    const struct cwiki_action_registry *actions,
    enum cwiki_keymap_mode mode, const struct cwiki_input_event *prefix,
    size_t prefix_count, const struct cwiki_float_area *area,
    enum cwiki_float_border border, const char *title, char *bytes,
    size_t capacity, size_t *length)
{
   struct output out = {NULL, 0U, 0};
   bool bordered = border == CWIKI_FLOAT_BORDER_SINGLE;
   size_t minimum_rows = bordered ? 3U : 1U;
   size_t minimum_columns = bordered ? 5U : 3U;

   if (keymap == NULL || actions == NULL ||
       (prefix == NULL && prefix_count != 0U) ||
       prefix_count > CWIKI_KEYMAP_MAX_SEQUENCE || area == NULL ||
       length == NULL || (bytes == NULL && capacity != 0U) ||
       (border != CWIKI_FLOAT_BORDER_NONE &&
       border != CWIKI_FLOAT_BORDER_SINGLE) || area->top == 0U ||
       area->left == 0U || area->rows < minimum_rows ||
       area->columns < minimum_columns || area->rows > 4096U ||
       area->columns > 4096U || area->top > 4097U - area->rows ||
       area->left > 4097U - area->columns) {
      errno = EINVAL;
      return -1;
   }
   overlay(&out, keymap, actions, mode, prefix, prefix_count, area, bordered,
       title);
   if (out.error != 0 || (bytes != NULL && capacity <= out.length)) {
      errno = out.error != 0 ? out.error : ENOSPC;
      return -1;
   }
   if (bytes != NULL) {
      out.bytes = bytes;
      out.length = 0U;
      overlay(&out, keymap, actions, mode, prefix, prefix_count, area,
          bordered, title);
      bytes[out.length] = '\0';
   }
   *length = out.length;
   return 0;
}
