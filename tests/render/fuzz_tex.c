/* Fuzz target for frontmatter parse, availability, extraction and TeX
 * generation (delivery d-20261012-251ff8, AC25). Invariants abort. */
#include "buffer.h"
#include "config.h"
#include "tex_block.h"
#include "tex_env.h"
#include "tex_source.h"
#include "zone.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static const char *const tokens[] = {
   "---\n", "$", "$$", "\\[", "\\]", "\\(", "\\)", "\\begin{", "\\end{",
   "align", "tikzcd", "foo", "}", "%%", "% cwiki: norender\n", "% cwiki: x\n",
   "```latex\n", "```\n", "<!-- ", "-->", "\\newenvironment{foo}",
   "\\usepackage{amsmath}", "packages: [tikz-cd]\n", "preamble: |\n  ",
   "engine: pdflatex\n", "\\ce{", "% VimTeX: SynIgnore on\n", "\n"
};
#define TOKEN_COUNT (sizeof tokens / sizeof tokens[0])

static size_t
count_lines(const char *text, size_t length)
{
   size_t n = 0U, i;

   for (i = 0U; i < length; i++) {
      n += text[i] == '\n';
   }
   return n + (length > 0U && text[length - 1U] != '\n');
}

static void
check_blocks(const struct cwiki_buffer *note,
    const struct cwiki_tex_extraction *x)
{
   size_t i;

   for (i = 0U; i < x->block_count; i++) {
      const struct cwiki_tex_block *b = &x->blocks[i];

      if (b->first_line > b->last_line || b->last_line >= note->line_count ||
          b->first_byte > note->lines[b->first_line].length ||
          b->last_end > note->lines[b->last_line].length ||
          (b->first_line == b->last_line && b->first_byte > b->last_end)) {
         abort();
      }
      if (i > 0U) {
         const struct cwiki_tex_block *p = &x->blocks[i - 1U];

         if (b->first_line < p->last_line ||
             (b->first_line == p->last_line && b->first_byte < p->last_end)) {
            abort();
         }
      }
   }
}

static void
check_source(const struct cwiki_buffer *note,
    const struct cwiki_config_note_tex *tex, size_t vault_lines,
    size_t chosen, const struct cwiki_tex_source *s, size_t block_count)
{
   size_t i, nl = 0U;
   size_t preamble_lines = tex->preamble == NULL ? 0U :
       count_lines(tex->preamble, tex->preamble_length);

   for (i = 0U; i < s->length; i++) {
      nl += s->text[i] == '\n';
   }
   if (s->line_count != nl && s->line_count != nl + 1U) {
      abort();
   }
   for (i = 0U; i < s->line_count; i++) {
      size_t l = s->lines[i].source_line;

      switch (s->lines[i].origin) {
      case CWIKI_TEX_ORIGIN_TEMPLATE:
         break;
      case CWIKI_TEX_ORIGIN_VAULT_PREAMBLE:
         if (l >= vault_lines) {
            abort();
         }
         break;
      case CWIKI_TEX_ORIGIN_PACKAGES:
         if (l >= tex->package_count) {
            abort();
         }
         break;
      case CWIKI_TEX_ORIGIN_NOTE_PREAMBLE:
         if (l >= preamble_lines) {
            abort();
         }
         break;
      case CWIKI_TEX_ORIGIN_BLOCK:
         if (l >= note->line_count) {
            abort();
         }
         break;
      }
   }
   if (s->page_count != chosen) {
      abort();
   }
   for (i = 0U; i < s->page_count; i++) {
      if (s->pages[i] >= block_count) {
         abort();
      }
   }
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
   struct cwiki_zone_engine *engine = NULL;
   struct cwiki_buffer note;
   struct cwiki_config_note_tex tex;
   struct cwiki_config_error error;
   struct cwiki_tex_extraction x;
   struct cwiki_tex_env_set *env = NULL;
   struct cwiki_tex_source src;
   const struct cwiki_zone_region *regions;
   uint64_t top_level;
   char *text, *vault;
   size_t region_count, used = 0U, vault_used = 0U, i, n = 0U, *chosen;
   enum cwiki_config_status rc;

   if (size > 2048U) {
      size = 2048U;
   }
   text = malloc(size * 24U + 1U);
   vault = malloc(size * 24U + 1U);
   if (text == NULL || vault == NULL) {
      free(text);
      free(vault);
      return 0;
   }
   for (i = 0U; i < size; i++) {
      const char *piece;
      size_t length;
      char one[2];
      char *dst = (data[i] & 0x80U) != 0U ? vault : text;
      size_t *at = (data[i] & 0x80U) != 0U ? &vault_used : &used;

      if ((data[i] & 3U) == 0U) {
         piece = tokens[(data[i] >> 2) % TOKEN_COUNT];
      } else {
         one[0] = " ab$\\{}[]%\n"[data[i] % 11U];
         one[1] = '\0';
         piece = one;
      }
      length = strlen(piece);
      memcpy(dst + *at, piece, length);
      *at += length;
   }
   memset(&tex, 0, sizeof tex);
   memset(&x, 0, sizeof x);
   regions = cwiki_zone_builtin_regions(&region_count, &top_level);
   if (cwiki_zone_engine_init(&engine, regions, region_count, top_level) != 0 ||
       cwiki_buffer_init(&note) != 0) {
      cwiki_zone_engine_free(engine);
      free(text);
      free(vault);
      return 0;
   }
   if (cwiki_buffer_load(&note, text, used) == 0) {
      rc = cwiki_config_parse_note_tex(&note, &tex, &error);
      if (rc != CWIKI_CONFIG_OK) {
         memset(&tex, 0, sizeof tex);
      }
      if (cwiki_tex_env_build(&env, vault, vault_used, tex.preamble,
              tex.preamble_length, (const char *const *)tex.packages,
              tex.package_count, NULL, 0U) != 0) {
         env = NULL;
      }
      if (cwiki_tex_extract(engine, &note, tex.body_first_line, env, &x) == 0) {
         check_blocks(&note, &x);
         chosen = malloc((x.block_count + 1U) * sizeof *chosen);
         for (i = 0U; chosen != NULL && i < x.block_count; i++) {
            if (x.blocks[i].compilable) {
               chosen[n++] = i;
            }
         }
         if (rc == CWIKI_CONFIG_OK && chosen != NULL &&
             cwiki_tex_generate(&note, &tex, vault, vault_used, &x, chosen, n,
                 &src) == 0) {
            check_source(&note, &tex, count_lines(vault, vault_used), n, &src,
                x.block_count);
            cwiki_tex_source_free(&src);
         }
         free(chosen);
         cwiki_tex_extraction_free(&x);
      }
      cwiki_tex_env_free(env);
      cwiki_config_note_tex_free(&tex);
   }
   cwiki_buffer_free(&note);
   cwiki_zone_engine_free(engine);
   free(text);
   free(vault);
   return 0;
}

#ifdef CWIKI_TEX_FUZZ_STANDALONE
#include <stdio.h>

static uint32_t
next_random(uint32_t *state)
{
   *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
   return *state;
}

int
main(void)
{
   uint8_t input[256];
   uint32_t state = UINT32_C(0x74657866);
   size_t iteration;

   for (iteration = 0U; iteration < 1500U; iteration++) {
      size_t length = (size_t)(next_random(&state) %
          (uint32_t)(sizeof(input) + 1U));
      size_t index;

      for (index = 0U; index < length; index++) {
         input[index] = (uint8_t)(next_random(&state) >> 8);
      }
      (void)LLVMFuzzerTestOneInput(input, length);
   }
   (void)puts("tex fuzz smoke: ok (1500 deterministic inputs)");
   return 0;
}
#endif
