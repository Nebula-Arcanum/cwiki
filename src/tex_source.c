#include "tex_source.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREAMBLE_CAP ((size_t)1U << 20)

struct gen {
   struct cwiki_tex_source *out;
   size_t text_cap;
   size_t line_cap;
};

static int
grow(void **ptr, size_t *cap, size_t need, size_t item)
{
   void *next;
   size_t new_cap = *cap == 0U ? 256U : *cap;

   if (need <= *cap) {
      return 0;
   }
   while (new_cap < need) {
      if (new_cap > ((size_t)-1) / 2U) {
         errno = ENOMEM;
         return -1;
      }
      new_cap *= 2U;
   }
   if (new_cap > ((size_t)-1) / item) {
      errno = ENOMEM;
      return -1;
   }
   next = realloc(*ptr, new_cap * item);
   if (next == NULL) {
      return -1;
   }
   *ptr = next;
   *cap = new_cap;
   return 0;
}

static int
add_line(struct gen *g, const char *bytes, size_t length,
    enum cwiki_tex_origin origin, size_t source_line)
{
   struct cwiki_tex_source *s = g->out;
   void *p;

   p = s->text;
   if (grow(&p, &g->text_cap, s->length + length + 2U, 1U) != 0) {
      return -1;
   }
   s->text = p;
   p = s->lines;
   if (grow(&p, &g->line_cap, s->line_count + 1U, sizeof(*s->lines)) != 0) {
      return -1;
   }
   s->lines = p;
   memcpy(s->text + s->length, bytes, length);
   s->length += length;
   s->text[s->length++] = '\n';
   s->text[s->length] = '\0';
   s->lines[s->line_count].origin = origin;
   s->lines[s->line_count].source_line = source_line;
   s->line_count++;
   return 0;
}

static int
add_str(struct gen *g, const char *s, enum cwiki_tex_origin origin,
    size_t source_line)
{
   return add_line(g, s, strlen(s), origin, source_line);
}

/* Adds each LF-separated line of text; a trailing LF adds no empty line. */
static int
add_text(struct gen *g, const char *text, size_t length,
    enum cwiki_tex_origin origin)
{
   size_t start = 0U;
   size_t index = 0U;

   while (start < length) {
      const char *nl = memchr(text + start, '\n', length - start);
      size_t end = nl == NULL ? length : (size_t)(nl - text);

      if (add_line(g, text + start, end - start, origin, index++) != 0) {
         return -1;
      }
      start = end + 1U;
   }
   return 0;
}

int
cwiki_tex_vault_preamble_read(const char *vault_root, char **bytes,
    size_t *length, char *message, size_t message_size)
{
   char path[4096];
   FILE *file;
   char *data;
   size_t got;
   int n;

   *bytes = NULL;
   *length = 0U;
   n = snprintf(path, sizeof(path), "%s/.cwiki/preamble.tex", vault_root);
   if (n < 0 || (size_t)n >= sizeof(path)) {
      (void)snprintf(message, message_size, "vault preamble path too long");
      return -1;
   }
   file = fopen(path, "rb");
   if (file == NULL) {
      if (errno == ENOENT) {
         return 0;
      }
      (void)snprintf(message, message_size, "cannot read %s", path);
      return -1;
   }
   data = malloc(PREAMBLE_CAP + 1U);
   if (data == NULL) {
      (void)fclose(file);
      return -1;
   }
   got = fread(data, 1U, PREAMBLE_CAP + 1U, file);
   if (ferror(file) != 0 || got > PREAMBLE_CAP) {
      (void)snprintf(message, message_size, "cannot read %s%s", path,
          got > PREAMBLE_CAP ? " (over 1 MiB)" : "");
      (void)fclose(file);
      free(data);
      return -1;
   }
   (void)fclose(file);
   data[got] = '\0';
   *bytes = data;
   *length = got;
   return 0;
}

int
cwiki_tex_generate(const struct cwiki_buffer *note,
    const struct cwiki_config_note_tex *tex, const char *vault,
    size_t vault_length, const struct cwiki_tex_extraction *blocks,
    const size_t *chosen, size_t chosen_count, struct cwiki_tex_source *out)
{
   struct gen g = {out, 0U, 0U};
   size_t i;

   memset(out, 0, sizeof(*out));
   if (tex == NULL || blocks == NULL || (chosen_count > 0U && chosen == NULL)) {
      errno = EINVAL;
      return -1;
   }
   for (i = 0U; i < chosen_count; i++) {
      if (chosen[i] >= blocks->block_count ||
          !blocks->blocks[chosen[i]].compilable) {
         errno = EINVAL;
         return -1;
      }
   }
   out->pages = malloc((chosen_count == 0U ? 1U : chosen_count) *
       sizeof(*out->pages));
   if (out->pages == NULL) {
      return -1;
   }
   if (add_str(&g, "\\documentclass[varwidth,multi=cwikiblock]{standalone}", CWIKI_TEX_ORIGIN_TEMPLATE, 0U) != 0) {
      goto fail;
   }
   if (vault != NULL &&
       add_text(&g, vault, vault_length, CWIKI_TEX_ORIGIN_VAULT_PREAMBLE) != 0) {
      goto fail;
   }
   for (i = 0U; i < tex->package_count; i++) {
      size_t name_length = strlen(tex->packages[i]);
      size_t n = name_length + sizeof("\\usepackage{}") - 1U;
      char *line = malloc(n + 1U);
      int status;

      if (line == NULL) {
         goto fail;
      }
      memcpy(line, "\\usepackage{", 12U);
      memcpy(line + 12U, tex->packages[i], name_length);
      memcpy(line + 12U + name_length, "}", 2U);
      status = add_line(&g, line, n, CWIKI_TEX_ORIGIN_PACKAGES, i);
      free(line);
      if (status != 0) {
         goto fail;
      }
   }
   if (tex->preamble != NULL &&
       add_text(&g, tex->preamble, tex->preamble_length,
           CWIKI_TEX_ORIGIN_NOTE_PREAMBLE) != 0) {
      goto fail;
   }
   if (add_str(&g, "\\begin{document}", CWIKI_TEX_ORIGIN_TEMPLATE, 0U) != 0) {
      goto fail;
   }
   for (i = 0U; i < chosen_count; i++) {
      const struct cwiki_tex_block *b = &blocks->blocks[chosen[i]];
      char *bytes = NULL;
      size_t length = 0U;
      size_t start = 0U;
      size_t line = b->first_line;

      if (cwiki_tex_block_bytes(note, b, &bytes, &length) != 0) {
         goto fail;
      }
      if (add_str(&g, "\\begin{cwikiblock}", CWIKI_TEX_ORIGIN_TEMPLATE,
              0U) != 0) {
         free(bytes);
         goto fail;
      }
      for (;;) {
         const char *nl = memchr(bytes + start, '\n', length - start);
         size_t end = nl == NULL ? length : (size_t)(nl - bytes);

         if (add_line(&g, bytes + start, end - start, CWIKI_TEX_ORIGIN_BLOCK,
                 line++) != 0) {
            free(bytes);
            goto fail;
         }
         if (nl == NULL) {
            break;
         }
         start = end + 1U;
      }
      free(bytes);
      if (add_str(&g, "\\end{cwikiblock}", CWIKI_TEX_ORIGIN_TEMPLATE,
              0U) != 0) {
         goto fail;
      }
      out->pages[out->page_count++] = chosen[i];
   }
   if (add_str(&g, "\\end{document}", CWIKI_TEX_ORIGIN_TEMPLATE, 0U) != 0) {
      goto fail;
   }
   return 0;
fail:
   {
      int saved = errno;

      cwiki_tex_source_free(out);
      errno = saved == 0 ? ENOMEM : saved;
   }
   return -1;
}

void
cwiki_tex_source_free(struct cwiki_tex_source *source)
{
   free(source->text);
   free(source->lines);
   free(source->pages);
   memset(source, 0, sizeof(*source));
}
