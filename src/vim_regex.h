#ifndef CWIKI_VIM_REGEX_H
#define CWIKI_VIM_REGEX_H

#include <stdbool.h>
#include <stddef.h>

enum cwiki_typed_regex_dialect {
   CWIKI_TYPED_REGEX_VIM,
   CWIKI_TYPED_REGEX_PCRE
};

enum cwiki_vim_regex_status {
   CWIKI_VIM_REGEX_OK,
   CWIKI_VIM_REGEX_INVALID_ARGUMENT,
   CWIKI_VIM_REGEX_INVALID_UTF8,
   CWIKI_VIM_REGEX_MALFORMED,
   CWIKI_VIM_REGEX_UNSUPPORTED,
   CWIKI_VIM_REGEX_NO_MEMORY
};

struct cwiki_vim_regex_result {
   enum cwiki_vim_regex_status status;
   char *pattern;
   size_t pattern_length;
   bool caseless;
   size_t source_offset;
   const char *diagnostic;
};

struct cwiki_vim_regex_result cwiki_vim_regex_translate(const char *pattern,
    size_t pattern_length, enum cwiki_typed_regex_dialect dialect);
void cwiki_vim_regex_result_destroy(struct cwiki_vim_regex_result *result);

#endif
