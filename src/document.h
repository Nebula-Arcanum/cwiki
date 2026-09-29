#ifndef CWIKI_DOCUMENT_H
#define CWIKI_DOCUMENT_H

#include "buffer.h"
#include "durable_write.h"

#include <stdbool.h>

struct cwiki_durable_write_test_fault;

struct cwiki_document {
   char *path;
   struct cwiki_buffer buffer;
   bool dirty;
};

int cwiki_document_init(struct cwiki_document *document, const char *path);
int cwiki_document_load(struct cwiki_document *document, const char *path);
void cwiki_document_free(struct cwiki_document *document);
void cwiki_document_mark_dirty(struct cwiki_document *document);
struct cwiki_durable_write_result cwiki_document_save(
    struct cwiki_document *document);

#ifdef CWIKI_DOCUMENT_TESTING
struct cwiki_durable_write_result cwiki_document_save_test(
    struct cwiki_document *document,
    const struct cwiki_durable_write_test_fault *fault);
#endif

#endif
