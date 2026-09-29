#include "vim_regex.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utf8proc.h>

enum magic_mode {
   MAGIC_VERY,
   MAGIC_DEFAULT,
   MAGIC_NOMAGIC,
   MAGIC_VERY_NOMAGIC
};

struct output {
   char *bytes;
   size_t length;
   size_t capacity;
};

struct parser {
   const char *source;
   size_t length;
   size_t offset;
   struct output output;
   enum magic_mode magic;
   size_t groups;
   size_t open_groups;
   bool can_repeat;
   bool uppercase;
   bool branch_start;
};

static struct cwiki_vim_regex_result
failure(enum cwiki_vim_regex_status status, size_t offset,
    const char *diagnostic)
{
   struct cwiki_vim_regex_result result = {
       status, NULL, 0U, false, offset, diagnostic};

   return result;
}

static bool
reserve(struct output *output, size_t extra)
{
   size_t required;
   size_t capacity;
   char *grown;

   if (extra > SIZE_MAX - output->length - 1U) {
      return false;
   }
   required = output->length + extra + 1U;
   if (required <= output->capacity && output->bytes != NULL) {
      return true;
   }
   capacity = output->capacity == 0U ? 32U : output->capacity;
   while (capacity < required) {
      if (capacity > SIZE_MAX / 2U) {
         capacity = required;
         break;
      }
      capacity *= 2U;
   }
   grown = realloc(output->bytes, capacity);
   if (grown == NULL) {
      return false;
   }
   output->bytes = grown;
   output->capacity = capacity;
   return true;
}

static bool
append_bytes(struct output *output, const char *bytes, size_t length)
{
   if (!reserve(output, length)) {
      return false;
   }
   if (length != 0U) {
      (void)memcpy(output->bytes + output->length, bytes, length);
   }
   output->length += length;
   output->bytes[output->length] = '\0';
   return true;
}

static bool
append_string(struct output *output, const char *string)
{
   return append_bytes(output, string, strlen(string));
}

static size_t
decode(const char *bytes, size_t length, utf8proc_int32_t *codepoint)
{
   utf8proc_ssize_t available = (utf8proc_ssize_t)(length < 4U ? length : 4U);
   utf8proc_ssize_t used = utf8proc_iterate(
       (const utf8proc_uint8_t *)bytes, available, codepoint);

   return used > 0 ? (size_t)used : 0U;
}

static size_t
invalid_utf8_offset(const char *bytes, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      utf8proc_int32_t codepoint;
      size_t used = decode(bytes + offset, length - offset, &codepoint);

      if (used == 0U) {
         return offset;
      }
      offset += used;
   }
   return SIZE_MAX;
}

static bool
is_uppercase(utf8proc_int32_t codepoint)
{
   utf8proc_category_t category = utf8proc_category(codepoint);

   return category == UTF8PROC_CATEGORY_LU || category == UTF8PROC_CATEGORY_LT;
}

static bool
pcre_special(unsigned char byte)
{
   return byte == '.' || byte == '^' || byte == '$' || byte == '*' ||
       byte == '+' || byte == '?' || byte == '(' || byte == ')' ||
       byte == '[' || byte == ']' || byte == '{' || byte == '}' ||
       byte == '\\' || byte == '|';
}

static bool
append_literal(struct parser *parser, size_t offset, size_t length,
    bool check_case)
{
   utf8proc_int32_t codepoint;

   if (check_case && decode(parser->source + offset, length, &codepoint) != 0U &&
       is_uppercase(codepoint)) {
      parser->uppercase = true;
   }
   if (length == 1U && pcre_special((unsigned char)parser->source[offset]) &&
       !append_string(&parser->output, "\\")) {
      return false;
   }
   return append_bytes(&parser->output, parser->source + offset, length);
}

static bool
property_supported(const char *name, size_t length)
{
   static const char *const names[] = {
       "C", "Cc", "Cf", "Cn", "Co", "Cs", "L", "Ll", "Lm", "Lo",
       "Lt", "Lu", "M", "Mc", "Me", "Mn", "N", "Nd", "Nl", "No",
       "P", "Pc", "Pd", "Pe", "Pf", "Pi", "Po", "Ps", "S", "Sc",
       "Sk", "Sm", "So", "Z", "Zl", "Zp", "Zs", "Xan", "Xps",
       "Xsp", "Xuc", "Xwd", "Greek", "Latin", "Cyrillic", "Arabic",
       "Hebrew", "Han", "Hiragana", "Katakana", "Hangul", "Common",
       "Inherited"};
   size_t index;

   for (index = 0U; index < sizeof(names) / sizeof(names[0]); index++) {
      if (strlen(names[index]) == length &&
          memcmp(name, names[index], length) == 0) {
         return true;
      }
   }
   return false;
}

static const char *
class_escape(unsigned char byte)
{
   switch (byte) {
   case 'd': return "\\d";
   case 'D': return "\\D";
   case 'w': return "\\w";
   case 'W': return "\\W";
   case 's': return "[ \\t]";
   case 'S': return "[^ \\t\\r\\n]";
   case 'x': return "[0-9A-Fa-f]";
   case 'X': return "[^0-9A-Fa-f]";
   case 'o': return "[0-7]";
   case 'O': return "[^0-7]";
   case 'h': return "[A-Za-z_]";
   case 'H': return "[^A-Za-z_]";
   case 'a': return "[A-Za-z]";
   case 'A': return "[^A-Za-z]";
   case 'l': return "\\p{Ll}";
   case 'L': return "\\P{Ll}";
   case 'u': return "\\p{Lu}";
   case 'U': return "\\P{Lu}";
   default: return NULL;
   }
}

static struct cwiki_vim_regex_result
parse_collection(struct parser *parser, size_t start, size_t opener_length)
{
   size_t cursor = start + opener_length;
   utf8proc_int32_t previous = 0;
   bool first = true;
   bool have_previous = false;
   bool range_pending = false;

   if (!append_string(&parser->output, "[")) {
      return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
   }
   if (cursor < parser->length && parser->source[cursor] == '^') {
      if (!append_string(&parser->output, "^")) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
      }
      cursor++;
   }
   while (cursor < parser->length) {
      unsigned char byte = (unsigned char)parser->source[cursor];

      if (byte == ']' && !first) {
         if (!append_string(&parser->output, "]")) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
         }
         parser->offset = cursor + 1U;
         parser->can_repeat = true;
         parser->branch_start = false;
         return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      }
      if (byte == '[' && cursor + 1U < parser->length &&
          (parser->source[cursor + 1U] == '=' ||
           parser->source[cursor + 1U] == '.')) {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, cursor,
             parser->source[cursor + 1U] == '=' ?
             "Vim collection equivalence class" :
             "Vim collection collating element");
      }
      if (byte == '[' && cursor + 1U < parser->length &&
          parser->source[cursor + 1U] == ':') {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, cursor,
             "Vim POSIX character class");
      }
      if (byte == '\\') {
         unsigned char escaped;
         utf8proc_int32_t current;

         if (cursor + 1U >= parser->length) {
            return failure(CWIKI_VIM_REGEX_MALFORMED, cursor,
                "trailing backslash in collection");
         }
         escaped = (unsigned char)parser->source[cursor + 1U];
         if (escaped != ']' && escaped != '\\' && escaped != '-' &&
             escaped != '^' && escaped != 'n' && escaped != 't' &&
             escaped != 'r') {
            return failure(CWIKI_VIM_REGEX_UNSUPPORTED, cursor,
                "unsupported Vim collection escape");
         }
         if (!append_bytes(&parser->output, parser->source + cursor, 2U)) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
         }
         current = escaped == 'n' ? '\n' :
             (escaped == 't' ? '\t' : (escaped == 'r' ? '\r' : escaped));
         if (range_pending && previous > current) {
            return failure(CWIKI_VIM_REGEX_MALFORMED, cursor,
                "descending Vim collection range");
         }
         previous = current;
         have_previous = true;
         range_pending = false;
         cursor += 2U;
         first = false;
         continue;
      }
      if ((byte == '[' || (first && (byte == '.' || byte == '=' ||
           byte == ':'))) && !append_string(&parser->output, "\\")) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
      }
      if (byte == '-' && first) {
         if (!append_string(&parser->output, "\\-")) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
         }
         previous = '-';
         have_previous = true;
         cursor++;
      } else if (byte == '-' && have_previous && cursor + 1U < parser->length &&
          parser->source[cursor + 1U] != ']') {
         if (!append_string(&parser->output, "-")) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
         }
         range_pending = true;
         have_previous = false;
         cursor++;
      } else {
         utf8proc_int32_t codepoint;
         size_t used = decode(parser->source + cursor,
             parser->length - cursor, &codepoint);

         if (range_pending && previous > codepoint) {
            return failure(CWIKI_VIM_REGEX_MALFORMED, cursor,
                "descending Vim collection range");
         }
         if (is_uppercase(codepoint)) {
            parser->uppercase = true;
         }
         if (!append_bytes(&parser->output, parser->source + cursor, used)) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, cursor, "out of memory");
         }
         previous = codepoint;
         have_previous = true;
         range_pending = false;
         cursor += used;
      }
      first = false;
   }
   return failure(CWIKI_VIM_REGEX_MALFORMED, start,
       "unterminated Vim collection");
}

static struct cwiki_vim_regex_result
append_class(struct parser *parser, unsigned char byte, size_t offset,
    bool include_newline)
{
   const char *translated = class_escape(byte);

   if (translated == NULL) {
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, offset,
          "unsupported Vim character class");
   }
   if (include_newline && !append_string(&parser->output, "(?:")) {
      return failure(CWIKI_VIM_REGEX_NO_MEMORY, offset, "out of memory");
   }
   if (!append_string(&parser->output, translated) ||
       (include_newline && !append_string(&parser->output, "|\\n)"))) {
      return failure(CWIKI_VIM_REGEX_NO_MEMORY, offset, "out of memory");
   }
   parser->can_repeat = true;
   parser->branch_start = false;
   return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
}

static struct cwiki_vim_regex_result
append_count(struct parser *parser, size_t start, size_t opener_length)
{
   size_t cursor = start + opener_length;
   size_t first_start;
   size_t first_length;
   size_t second_start = 0U;
   size_t second_length = 0U;
   uint32_t first_value = 0U;
   uint32_t second_value = 0U;
   bool comma = false;
   bool lazy = false;

   if (!parser->can_repeat) {
      return failure(CWIKI_VIM_REGEX_MALFORMED, start,
          "Vim repetition has no preceding atom");
   }
   if (cursor < parser->length && parser->source[cursor] == '-') {
      lazy = true;
      cursor++;
   }
   first_start = cursor;
   while (cursor < parser->length && parser->source[cursor] >= '0' &&
       parser->source[cursor] <= '9') {
      if (first_value > UINT32_C(6553) ||
          (first_value == UINT32_C(6553) && parser->source[cursor] > '5')) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, cursor,
             "Vim repetition bound exceeds 65535");
      }
      first_value = first_value * 10U +
          (uint32_t)(parser->source[cursor] - '0');
      cursor++;
   }
   first_length = cursor - first_start;
   if (cursor < parser->length && parser->source[cursor] == ',') {
      comma = true;
      cursor++;
      second_start = cursor;
      while (cursor < parser->length && parser->source[cursor] >= '0' &&
          parser->source[cursor] <= '9') {
         if (second_value > UINT32_C(6553) ||
             (second_value == UINT32_C(6553) && parser->source[cursor] > '5')) {
            return failure(CWIKI_VIM_REGEX_MALFORMED, cursor,
                "Vim repetition bound exceeds 65535");
         }
         second_value = second_value * 10U +
             (uint32_t)(parser->source[cursor] - '0');
         cursor++;
      }
      second_length = cursor - second_start;
   }
   if (cursor >= parser->length || parser->source[cursor] != '}') {
      return failure(CWIKI_VIM_REGEX_MALFORMED, start,
          "malformed Vim counted repetition");
   }
   if (!comma && first_length == 0U) {
      if (!append_string(&parser->output, lazy ? "*?" : "*")) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
   } else {
      if (comma && first_length != 0U && second_length != 0U &&
          first_value > second_value) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "descending Vim repetition bounds");
      }
      if (!append_string(&parser->output, "{") ||
          (first_length == 0U && !append_string(&parser->output, "0")) ||
          !append_bytes(&parser->output, parser->source + first_start,
              first_length) ||
          (comma && (!append_string(&parser->output, ",") ||
              !append_bytes(&parser->output, parser->source + second_start,
                  second_length))) ||
          !append_string(&parser->output, lazy ? "}?" : "}")) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
   }
   parser->offset = cursor + 1U;
   parser->can_repeat = false;
   return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
}

static struct cwiki_vim_regex_result
append_property(struct parser *parser, size_t start, unsigned char kind)
{
   size_t name_start = start + 3U;
   size_t cursor = name_start;

   while (cursor < parser->length && parser->source[cursor] != '}') {
      cursor++;
   }
   if (cursor == parser->length || cursor == name_start) {
      return failure(CWIKI_VIM_REGEX_MALFORMED, start,
          "malformed Unicode property escape");
   }
   if (!property_supported(parser->source + name_start, cursor - name_start)) {
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "unsupported Unicode property");
   }
   if (!append_string(&parser->output, kind == 'p' ? "\\p{" : "\\P{") ||
       !append_bytes(&parser->output, parser->source + name_start,
           cursor - name_start) ||
       !append_string(&parser->output, "}")) {
      return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
   }
   parser->offset = cursor + 1U;
   parser->can_repeat = true;
   parser->branch_start = false;
   return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
}

static bool
at_branch_end(const struct parser *parser)
{
   enum magic_mode magic = parser->magic;
   size_t cursor = parser->offset;

   while (cursor + 1U < parser->length && parser->source[cursor] == '\\' &&
       (parser->source[cursor + 1U] == 'v' ||
        parser->source[cursor + 1U] == 'm' ||
        parser->source[cursor + 1U] == 'M' ||
        parser->source[cursor + 1U] == 'V')) {
      switch (parser->source[cursor + 1U]) {
      case 'v': magic = MAGIC_VERY; break;
      case 'm': magic = MAGIC_DEFAULT; break;
      case 'M': magic = MAGIC_NOMAGIC; break;
      default: magic = MAGIC_VERY_NOMAGIC; break;
      }
      cursor += 2U;
   }
   if (cursor == parser->length) {
      return true;
   }
   if (magic == MAGIC_VERY) {
      return parser->source[cursor] == '|' || parser->source[cursor] == ')';
   }
   return cursor + 1U < parser->length && parser->source[cursor] == '\\' &&
       (parser->source[cursor + 1U] == '|' ||
        parser->source[cursor + 1U] == ')');
}

static bool
escaped_meta_is_special(enum magic_mode mode, unsigned char byte)
{
   if (mode == MAGIC_VERY) {
      return false;
   }
   if (byte == '(' || byte == ')' || byte == '|' || byte == '+' ||
       byte == '?' || byte == '=' || byte == '{' || byte == '<' ||
       byte == '>') {
      return true;
   }
   if ((byte == '.' || byte == '[' || byte == '*') &&
       (mode == MAGIC_NOMAGIC || mode == MAGIC_VERY_NOMAGIC)) {
      return true;
   }
   if ((byte == '^' || byte == '$') && mode == MAGIC_VERY_NOMAGIC) {
      return true;
   }
   return false;
}

static struct cwiki_vim_regex_result
parse_escaped(struct parser *parser)
{
   size_t start = parser->offset;
   unsigned char byte;
   const char *translated;

   if (start + 1U >= parser->length) {
      return failure(CWIKI_VIM_REGEX_MALFORMED, start,
          "trailing Vim backslash");
   }
   byte = (unsigned char)parser->source[start + 1U];
   parser->offset += 2U;
   switch (byte) {
   case 'v': parser->magic = MAGIC_VERY; return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case 'm': parser->magic = MAGIC_DEFAULT; return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case 'M': parser->magic = MAGIC_NOMAGIC; return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case 'V': parser->magic = MAGIC_VERY_NOMAGIC; return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case 'z':
      if (start + 2U < parser->length &&
          (parser->source[start + 2U] == 's' ||
           parser->source[start + 2U] == 'e')) {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
             parser->source[start + 2U] == 's' ? "Vim \\zs" : "Vim \\ze");
      }
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "Vim external match reference");
   case '%':
      if (parser->magic == MAGIC_VERY) {
         if (!append_literal(parser, start + 1U, 1U, false)) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
         }
         parser->can_repeat = true;
         parser->branch_start = false;
         return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      }
      if (start + 2U < parser->length && parser->source[start + 2U] == 'V') {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start, "Vim \\%V");
      }
      if (start + 2U < parser->length && parser->source[start + 2U] == 'd') {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
             "Vim decimal character atom \\%d123");
      }
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "unsupported Vim \\% atom");
   case '@':
      if (parser->magic == MAGIC_VERY) {
         if (!append_literal(parser, start + 1U, 1U, false)) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
         }
         parser->can_repeat = true;
         parser->branch_start = false;
         return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      }
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "Vim lookaround atom \\@");
   case '~':
      if (parser->magic == MAGIC_NOMAGIC ||
          parser->magic == MAGIC_VERY_NOMAGIC) {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
             "Vim previous-substitute atom");
      }
      if (!append_literal(parser, start + 1U, 1U, false)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case '&':
      if (parser->magic != MAGIC_VERY) {
         return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
             "Vim branch conjunction atom");
      }
      if (!append_literal(parser, start + 1U, 1U, false)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   case 'c': case 'C':
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "Vim inline case override");
   case 'Z':
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "Vim combining-character ignore atom \\Z");
   case '_':
      if (parser->offset >= parser->length) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "incomplete Vim newline-inclusive atom");
      }
      byte = (unsigned char)parser->source[parser->offset++];
      if (byte == '.') {
         if (!append_string(&parser->output, "[\\s\\S]")) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
         }
         parser->can_repeat = true;
         parser->branch_start = false;
         return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      }
      return append_class(parser, byte, start, true);
   default: break;
   }
   if ((byte == 'p' || byte == 'P') && parser->offset < parser->length &&
       parser->source[parser->offset] == '{') {
      return append_property(parser, start, byte);
   }
   translated = class_escape(byte);
   if (translated != NULL) {
      if (!append_string(&parser->output, translated)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   }
   if (byte >= '1' && byte <= '9') {
      if ((size_t)(byte - '0') > parser->groups) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "Vim backreference has no preceding group");
      }
      if (!append_bytes(&parser->output, parser->source + start, 2U)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   }
   if (byte == 'n' || byte == 't' || byte == 'r') {
      if (!append_bytes(&parser->output, parser->source + start, 2U)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   }
   if (byte == 'e' || byte == 'b') {
      if (!append_string(&parser->output,
          byte == 'e' ? "\\x{1b}" : "\\x{08}")) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
      }
      parser->can_repeat = true;
      parser->branch_start = false;
      return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   }
   if (!escaped_meta_is_special(parser->magic, byte)) {
      if (byte == '\\' || byte == '/' || pcre_special(byte)) {
         if (!append_literal(parser, start + 1U, 1U, false)) {
            return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
         }
         parser->can_repeat = true;
         parser->branch_start = false;
         return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      }
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "unsupported Vim escape");
   }
   switch (byte) {
   case '.':
      if (!append_string(&parser->output, ".")) goto no_memory;
      parser->can_repeat = true;
      parser->branch_start = false;
      break;
   case '[':
      return parse_collection(parser, start, 2U);
   case '(':
      if (!append_string(&parser->output, "(")) goto no_memory;
      parser->groups++;
      parser->open_groups++;
      parser->can_repeat = false;
      parser->branch_start = true;
      break;
   case ')':
      if (parser->open_groups == 0U) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "unmatched Vim group close");
      }
      if (!append_string(&parser->output, ")")) goto no_memory;
      parser->open_groups--;
      parser->can_repeat = true;
      parser->branch_start = false;
      break;
   case '|':
      if (!append_string(&parser->output, "|")) goto no_memory;
      parser->can_repeat = false;
      parser->branch_start = true;
      break;
   case '*': case '+': case '?': case '=':
      if (!parser->can_repeat) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "Vim quantifier has no preceding atom");
      }
      if (!append_bytes(&parser->output, byte == '=' ? "?" :
          parser->source + start + 1U, 1U)) goto no_memory;
      parser->can_repeat = false;
      break;
   case '{': return append_count(parser, start, 2U);
   case '<': case '>':
      if (!append_string(&parser->output, byte == '<' ?
          "(?<!\\w)(?=\\w)" : "(?<=\\w)(?!\\w)")) goto no_memory;
      parser->can_repeat = false;
      parser->branch_start = false;
      break;
   case '^': case '$':
      if (!append_string(&parser->output,
          byte == '^' ? "(?m:^)" : "(?m:$)")) goto no_memory;
      parser->can_repeat = false;
      parser->branch_start = false;
      break;
   default:
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "unsupported Vim metacharacter");
   }
   return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");

no_memory:
   return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
}

static bool
unescaped_special(enum magic_mode mode, unsigned char byte)
{
   if (mode == MAGIC_VERY) {
      return byte == '.' || byte == '[' || byte == '(' || byte == ')' ||
          byte == '|' || byte == '*' || byte == '+' || byte == '?' ||
          byte == '=' || byte == '{' || byte == '^' || byte == '$' ||
          byte == '<' || byte == '>';
   }
   if (mode == MAGIC_DEFAULT) {
      return byte == '.' || byte == '[' || byte == '*' || byte == '^' ||
          byte == '$';
   }
   return mode == MAGIC_NOMAGIC && (byte == '^' || byte == '$');
}

static struct cwiki_vim_regex_result
parse_unescaped_special(struct parser *parser)
{
   size_t start = parser->offset;
   unsigned char byte = (unsigned char)parser->source[parser->offset++];

   switch (byte) {
   case '.':
      if (!append_string(&parser->output, ".")) goto no_memory;
      parser->can_repeat = true;
      parser->branch_start = false;
      break;
   case '[': return parse_collection(parser, start, 1U);
   case '(':
      if (!append_string(&parser->output, "(")) goto no_memory;
      parser->groups++;
      parser->open_groups++;
      parser->can_repeat = false;
      parser->branch_start = true;
      break;
   case ')':
      if (parser->open_groups == 0U) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "unmatched Vim group close");
      }
      if (!append_string(&parser->output, ")")) goto no_memory;
      parser->open_groups--;
      parser->can_repeat = true;
      parser->branch_start = false;
      break;
   case '|':
      if (!append_string(&parser->output, "|")) goto no_memory;
      parser->can_repeat = false;
      parser->branch_start = true;
      break;
   case '*': case '+': case '?': case '=':
      if (!parser->can_repeat) {
         return failure(CWIKI_VIM_REGEX_MALFORMED, start,
             "Vim quantifier has no preceding atom");
      }
      if (!append_bytes(&parser->output, byte == '=' ? "?" :
          parser->source + start, 1U)) goto no_memory;
      parser->can_repeat = false;
      break;
   case '{': return append_count(parser, start, 1U);
   case '<': case '>':
      if (!append_string(&parser->output, byte == '<' ?
          "(?<!\\w)(?=\\w)" : "(?<=\\w)(?!\\w)")) goto no_memory;
      parser->can_repeat = false;
      parser->branch_start = false;
      break;
   case '^': case '$':
      if ((parser->magic == MAGIC_DEFAULT ||
           parser->magic == MAGIC_NOMAGIC) &&
          ((byte == '^' && !parser->branch_start) ||
           (byte == '$' && !at_branch_end(parser)))) {
         if (!append_literal(parser, start, 1U, false)) goto no_memory;
         parser->can_repeat = true;
         parser->branch_start = false;
      } else {
         if (!append_string(&parser->output,
             byte == '^' ? "(?m:^)" : "(?m:$)")) goto no_memory;
         parser->can_repeat = false;
         parser->branch_start = false;
      }
      break;
   default:
      return failure(CWIKI_VIM_REGEX_UNSUPPORTED, start,
          "unsupported Vim metacharacter");
   }
   return failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");

no_memory:
   return failure(CWIKI_VIM_REGEX_NO_MEMORY, start, "out of memory");
}

static bool
pcre_smartcase(const char *pattern, size_t length)
{
   size_t offset = 0U;

   while (offset < length) {
      utf8proc_int32_t codepoint;
      size_t used;

      if (pattern[offset] == '(' && offset + 2U < length &&
          pattern[offset + 1U] == '?' && pattern[offset + 2U] == '<' &&
          offset + 3U < length && pattern[offset + 3U] != '=' &&
          pattern[offset + 3U] != '!') {
         offset += 3U;
         while (offset < length && pattern[offset] != '>') {
            offset++;
         }
         if (offset < length) {
            offset++;
         }
         continue;
      }
      if (pattern[offset] == '(' && offset + 2U < length &&
          pattern[offset + 1U] == '?' && pattern[offset + 2U] == '\'') {
         offset += 3U;
         while (offset < length && pattern[offset] != '\'') {
            offset++;
         }
         if (offset < length) {
            offset++;
         }
         continue;
      }
      if (pattern[offset] == '(' && offset + 3U < length &&
          pattern[offset + 1U] == '?' && pattern[offset + 2U] == 'P' &&
          pattern[offset + 3U] == '<') {
         offset += 4U;
         while (offset < length && pattern[offset] != '>') {
            offset++;
         }
         if (offset < length) {
            offset++;
         }
         continue;
      }
      if (pattern[offset] == '(' && offset + 2U < length &&
          pattern[offset + 1U] == '*') {
         offset += 2U;
         while (offset < length && pattern[offset] != ')') {
            offset++;
         }
         if (offset < length) {
            offset++;
         }
         continue;
      }
      if (pattern[offset] == '\\' && offset + 1U < length) {
         unsigned char escaped = (unsigned char)pattern[offset + 1U];

         offset += 2U;
         if ((escaped == 'p' || escaped == 'P' || escaped == 'N' ||
              escaped == 'x' || escaped == 'o' || escaped == 'g' ||
              escaped == 'k') && offset < length &&
             (pattern[offset] == '{' || pattern[offset] == '<' ||
              pattern[offset] == '\'')) {
            char close = pattern[offset] == '{' ? '}' :
                (pattern[offset] == '<' ? '>' : '\'');

            offset++;
            while (offset < length && pattern[offset] != close) {
               offset++;
            }
            if (offset < length) {
               offset++;
            }
         }
         continue;
      }
      used = decode(pattern + offset, length - offset, &codepoint);
      if (used == 0U) {
         return false;
      }
      if (is_uppercase(codepoint)) {
         return false;
      }
      offset += used;
   }
   return true;
}

struct cwiki_vim_regex_result
cwiki_vim_regex_translate(const char *pattern, size_t pattern_length,
    enum cwiki_typed_regex_dialect dialect)
{
   struct parser parser = {pattern, pattern_length, 0U, {NULL, 0U, 0U},
       MAGIC_DEFAULT, 0U, 0U, false, false, true};
   struct cwiki_vim_regex_result result;
   size_t bad_utf8;

   if ((pattern == NULL && pattern_length != 0U) ||
       (dialect != CWIKI_TYPED_REGEX_VIM &&
        dialect != CWIKI_TYPED_REGEX_PCRE)) {
      return failure(CWIKI_VIM_REGEX_INVALID_ARGUMENT, 0U,
          "invalid translator argument");
   }
   if (pattern == NULL) {
      pattern = "";
      parser.source = pattern;
   }
   bad_utf8 = invalid_utf8_offset(pattern, pattern_length);
   if (bad_utf8 != SIZE_MAX) {
      return failure(CWIKI_VIM_REGEX_INVALID_UTF8, bad_utf8,
          "invalid UTF-8 in typed pattern");
   }
   if (dialect == CWIKI_TYPED_REGEX_PCRE) {
      if (!append_bytes(&parser.output, pattern, pattern_length)) {
         return failure(CWIKI_VIM_REGEX_NO_MEMORY, 0U, "out of memory");
      }
      result = failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
      result.pattern = parser.output.bytes;
      result.pattern_length = parser.output.length;
      result.caseless = pcre_smartcase(pattern, pattern_length);
      return result;
   }
   while (parser.offset < parser.length) {
      unsigned char byte = (unsigned char)parser.source[parser.offset];

      if ((parser.magic == MAGIC_DEFAULT && byte == '~') ||
          (parser.magic == MAGIC_VERY &&
           (byte == '~' || byte == '%' || byte == '&' || byte == '@'))) {
         result = failure(CWIKI_VIM_REGEX_UNSUPPORTED, parser.offset,
             byte == '~' ? "Vim previous-substitute atom" :
             (byte == '%' ? "unsupported Vim % atom" :
              (byte == '&' ? "Vim branch conjunction atom" :
               "Vim lookaround atom @")));
      } else if (byte == '\\') {
         result = parse_escaped(&parser);
      } else if (unescaped_special(parser.magic, byte)) {
         result = parse_unescaped_special(&parser);
      } else {
         utf8proc_int32_t codepoint;
         size_t used = decode(parser.source + parser.offset,
             parser.length - parser.offset, &codepoint);

         if (!append_literal(&parser, parser.offset, used, true)) {
            result = failure(CWIKI_VIM_REGEX_NO_MEMORY, parser.offset,
                "out of memory");
         } else {
            parser.offset += used;
            parser.can_repeat = true;
            parser.branch_start = false;
            result = failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
         }
      }
      if (result.status != CWIKI_VIM_REGEX_OK) {
         free(parser.output.bytes);
         return result;
      }
   }
   if (parser.open_groups != 0U) {
      free(parser.output.bytes);
      return failure(CWIKI_VIM_REGEX_MALFORMED, pattern_length,
          "unterminated Vim group");
   }
   if (parser.output.bytes == NULL && !append_bytes(&parser.output, "", 0U)) {
      return failure(CWIKI_VIM_REGEX_NO_MEMORY, 0U, "out of memory");
   }
   result = failure(CWIKI_VIM_REGEX_OK, SIZE_MAX, "");
   result.pattern = parser.output.bytes;
   result.pattern_length = parser.output.length;
   result.caseless = !parser.uppercase;
   return result;
}

void
cwiki_vim_regex_result_destroy(struct cwiki_vim_regex_result *result)
{
   if (result == NULL) {
      return;
   }
   free(result->pattern);
   result->pattern = NULL;
   result->pattern_length = 0U;
}
