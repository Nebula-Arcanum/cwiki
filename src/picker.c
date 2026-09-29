#include "picker.h"

#include "unicode.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

struct picker_entry {
   char *id;
   char *label;
   char *detail;
};

struct picker_match {
   size_t entry;
   size_t score;
};

struct cwiki_picker {
   struct picker_entry *entries;
   size_t entry_count;
   struct picker_match *matches;
   size_t match_count;
   size_t selected;
   struct cwiki_line_edit *query;
};

static bool
valid_string(const char *text, bool allow_empty)
{
   size_t i;
   size_t length;

   if (text == NULL || (!allow_empty && text[0] == '\0')) {
      return false;
   }
   length = strlen(text);
   if (!cwiki_utf8_validate(text, length)) {
      return false;
   }
   for (i = 0U; i < length; i++) {
      unsigned char byte = (unsigned char)text[i];

      if (byte < 0x20U || byte == 0x7fU) {
         return false;
      }
   }
   return true;
}

static char *
copy_string(const char *text)
{
   size_t length = strlen(text);
   char *copy;

   if (length == SIZE_MAX) {
      return NULL;
   }
   copy = malloc(length + 1U);
   if (copy != NULL) {
      (void)memcpy(copy, text, length + 1U);
   }
   return copy;
}

static void
entry_free(struct picker_entry *entry)
{
   free(entry->id);
   free(entry->label);
   free(entry->detail);
}

static bool
duplicate_id(const struct cwiki_picker_spec *items, size_t index)
{
   size_t i;

   for (i = 0U; i < index; i++) {
      if (strcmp(items[i].id, items[index].id) == 0) {
         return true;
      }
   }
   return false;
}

static bool
valid_specs(const struct cwiki_picker_spec *items, size_t count)
{
   size_t i;

   if (count != 0U && items == NULL) {
      return false;
   }
   for (i = 0U; i < count; i++) {
      if (!valid_string(items[i].id, false) ||
          !valid_string(items[i].label, false) ||
          (items[i].detail != NULL &&
          !valid_string(items[i].detail, true)) || duplicate_id(items, i)) {
         return false;
      }
   }
   return true;
}

static bool
decode(const char *bytes, size_t length, size_t offset,
    utf8proc_int32_t *codepoint, size_t *used)
{
   utf8proc_ssize_t available = (utf8proc_ssize_t)((length - offset) < 4U ?
       (length - offset) : 4U);
   utf8proc_ssize_t decoded = utf8proc_iterate(
       (const utf8proc_uint8_t *)bytes + offset, available, codepoint);

   if (decoded <= 0) {
      return false;
   }
   *used = (size_t)decoded;
   return true;
}

static size_t
saturating_add(size_t left, size_t right)
{
   return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

static size_t
saturating_multiply(size_t value, size_t multiplier)
{
   return value > SIZE_MAX / multiplier ? SIZE_MAX : value * multiplier;
}

static bool
fuzzy_score(const char *label, const char *query, size_t query_length,
    size_t *score)
{
   size_t label_length = strlen(label);
   size_t label_offset = 0U;
   size_t query_offset = 0U;
   size_t label_position = 0U;
   size_t first = 0U;
   size_t previous = 0U;
   size_t gaps = 0U;
   bool have_match = false;

   while (query_offset < query_length) {
      utf8proc_int32_t query_codepoint;
      size_t query_used;
      bool found = false;

      if (!decode(query, query_length, query_offset, &query_codepoint,
          &query_used)) {
         return false;
      }
      query_codepoint = utf8proc_tolower(query_codepoint);
      while (label_offset < label_length) {
         utf8proc_int32_t label_codepoint;
         size_t label_used;

         if (!decode(label, label_length, label_offset, &label_codepoint,
             &label_used)) {
            return false;
         }
         label_offset += label_used;
         if (utf8proc_tolower(label_codepoint) == query_codepoint) {
            if (!have_match) {
               first = label_position;
            } else {
               gaps = saturating_add(gaps,
                   label_position - previous - 1U);
            }
            previous = label_position;
            have_match = true;
            found = true;
            label_position++;
            break;
         }
         label_position++;
      }
      if (!found) {
         return false;
      }
      query_offset += query_used;
   }
   *score = saturating_add(saturating_multiply(first, 16U),
       saturating_multiply(gaps, 4U));
   *score = saturating_add(*score, label_length);
   return true;
}

static bool
match_before(const struct cwiki_picker *picker,
    const struct picker_match *left, const struct picker_match *right)
{
   int labels;

   if (left->score != right->score) {
      return left->score < right->score;
   }
   labels = strcmp(picker->entries[left->entry].label,
       picker->entries[right->entry].label);
   return labels < 0 || (labels == 0 && left->entry < right->entry);
}

static void
insert_match(struct cwiki_picker *picker, struct picker_match match)
{
   size_t position = picker->match_count;

   while (position != 0U &&
       match_before(picker, &match, &picker->matches[position - 1U])) {
      picker->matches[position] = picker->matches[position - 1U];
      position--;
   }
   picker->matches[position] = match;
   picker->match_count++;
}

static void
refilter(struct cwiki_picker *picker)
{
   const char *query = cwiki_line_edit_bytes(picker->query);
   size_t query_length = cwiki_line_edit_length(picker->query);
   size_t previous_entry = SIZE_MAX;
   size_t i;

   if (picker->selected < picker->match_count) {
      previous_entry = picker->matches[picker->selected].entry;
   }
   picker->match_count = 0U;
   for (i = 0U; i < picker->entry_count; i++) {
      struct picker_match match = {i, 0U};

      if (query_length == 0U) {
         picker->matches[picker->match_count++] = match;
      } else if (fuzzy_score(picker->entries[i].label, query, query_length,
          &match.score)) {
         insert_match(picker, match);
      }
   }
   picker->selected = 0U;
   for (i = 0U; i < picker->match_count; i++) {
      if (picker->matches[i].entry == previous_entry) {
         picker->selected = i;
         break;
      }
   }
}

enum cwiki_picker_status
cwiki_picker_init(struct cwiki_picker **picker,
    const struct cwiki_picker_spec *items, size_t count)
{
   struct cwiki_picker *created;
   size_t i;

   if (picker == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   *picker = NULL;
   if (!valid_specs(items, count) ||
       count > SIZE_MAX / sizeof(*created->entries) ||
       count > SIZE_MAX / sizeof(*created->matches)) {
      return CWIKI_PICKER_INVALID;
   }
   created = calloc(1U, sizeof(*created));
   if (created == NULL) {
      return CWIKI_PICKER_NO_MEMORY;
   }
   if (count != 0U) {
      created->entries = calloc(count, sizeof(*created->entries));
      created->matches = malloc(count * sizeof(*created->matches));
      if (created->entries == NULL || created->matches == NULL) {
         cwiki_picker_free(created);
         return CWIKI_PICKER_NO_MEMORY;
      }
   }
   created->entry_count = count;
   for (i = 0U; i < count; i++) {
      created->entries[i].id = copy_string(items[i].id);
      created->entries[i].label = copy_string(items[i].label);
      created->entries[i].detail = copy_string(
          items[i].detail == NULL ? "" : items[i].detail);
      if (created->entries[i].id == NULL ||
          created->entries[i].label == NULL ||
          created->entries[i].detail == NULL) {
         cwiki_picker_free(created);
         return CWIKI_PICKER_NO_MEMORY;
      }
   }
   if (cwiki_line_edit_init(&created->query, "", 0U) !=
       CWIKI_LINE_EDIT_OK) {
      cwiki_picker_free(created);
      return CWIKI_PICKER_NO_MEMORY;
   }
   refilter(created);
   *picker = created;
   return CWIKI_PICKER_OK;
}

void
cwiki_picker_free(struct cwiki_picker *picker)
{
   size_t i;

   if (picker == NULL) {
      return;
   }
   for (i = 0U; i < picker->entry_count; i++) {
      entry_free(&picker->entries[i]);
   }
   cwiki_line_edit_free(picker->query);
   free(picker->matches);
   free(picker->entries);
   free(picker);
}

const char *
cwiki_picker_query(const struct cwiki_picker *picker)
{
   return picker == NULL ? NULL : cwiki_line_edit_bytes(picker->query);
}

size_t
cwiki_picker_query_length(const struct cwiki_picker *picker)
{
   return picker == NULL ? 0U : cwiki_line_edit_length(picker->query);
}

size_t
cwiki_picker_query_cursor(const struct cwiki_picker *picker)
{
   return picker == NULL ? 0U : cwiki_line_edit_cursor(picker->query);
}

static enum cwiki_picker_status
line_status(enum cwiki_line_edit_status status)
{
   switch (status) {
   case CWIKI_LINE_EDIT_OK:
      return CWIKI_PICKER_OK;
   case CWIKI_LINE_EDIT_INVALID:
      return CWIKI_PICKER_INVALID;
   case CWIKI_LINE_EDIT_NO_MEMORY:
      return CWIKI_PICKER_NO_MEMORY;
   case CWIKI_LINE_EDIT_NOT_HANDLED:
      return CWIKI_PICKER_NOT_HANDLED;
   }
   return CWIKI_PICKER_INVALID;
}

enum cwiki_picker_status
cwiki_picker_insert(struct cwiki_picker *picker, const char *text,
    size_t length)
{
   enum cwiki_line_edit_status status;

   if (picker == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   status = cwiki_line_edit_insert(picker->query, text, length);
   if (status == CWIKI_LINE_EDIT_OK) {
      refilter(picker);
   }
   return line_status(status);
}

enum cwiki_picker_status
cwiki_picker_control(struct cwiki_picker *picker, uint32_t key)
{
   enum cwiki_line_edit_status status;

   if (picker == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   status = cwiki_line_edit_control(picker->query, key);
   if (status == CWIKI_LINE_EDIT_OK) {
      refilter(picker);
   }
   return line_status(status);
}

size_t
cwiki_picker_count(const struct cwiki_picker *picker)
{
   return picker == NULL ? 0U : picker->match_count;
}

enum cwiki_picker_status
cwiki_picker_at(const struct cwiki_picker *picker, size_t index,
    struct cwiki_picker_item *item)
{
   const struct picker_entry *entry;

   if (picker == NULL || item == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   if (index >= picker->match_count) {
      return CWIKI_PICKER_NOT_FOUND;
   }
   entry = &picker->entries[picker->matches[index].entry];
   item->id = entry->id;
   item->label = entry->label;
   item->detail = entry->detail;
   item->score = picker->matches[index].score;
   return CWIKI_PICKER_OK;
}

size_t
cwiki_picker_selected(const struct cwiki_picker *picker)
{
   return picker == NULL || picker->match_count == 0U ? SIZE_MAX :
       picker->selected;
}

enum cwiki_picker_status
cwiki_picker_select_next(struct cwiki_picker *picker)
{
   if (picker == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   if (picker->match_count == 0U) {
      return CWIKI_PICKER_NOT_FOUND;
   }
   if (picker->selected + 1U < picker->match_count) {
      picker->selected++;
   }
   return CWIKI_PICKER_OK;
}

enum cwiki_picker_status
cwiki_picker_select_previous(struct cwiki_picker *picker)
{
   if (picker == NULL) {
      return CWIKI_PICKER_INVALID;
   }
   if (picker->match_count == 0U) {
      return CWIKI_PICKER_NOT_FOUND;
   }
   if (picker->selected != 0U) {
      picker->selected--;
   }
   return CWIKI_PICKER_OK;
}
