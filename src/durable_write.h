#ifndef CWIKI_SRC_DURABLE_WRITE_H
#define CWIKI_SRC_DURABLE_WRITE_H

#include <stddef.h>

enum cwiki_durable_write_status {
   CWIKI_DURABLE_WRITE_SUCCESS,
   CWIKI_DURABLE_WRITE_PRE_RENAME_FAILURE,
   CWIKI_DURABLE_WRITE_DURABILITY_UNCERTAIN
};

enum cwiki_durable_write_operation {
   CWIKI_DURABLE_WRITE_NONE,
   CWIKI_DURABLE_WRITE_STAT,
   CWIKI_DURABLE_WRITE_CREATE_TEMP,
   CWIKI_DURABLE_WRITE_SET_MODE,
   CWIKI_DURABLE_WRITE_WRITE,
   CWIKI_DURABLE_WRITE_FILE_SYNC,
   CWIKI_DURABLE_WRITE_FILE_CLOSE,
   CWIKI_DURABLE_WRITE_DIRECTORY_OPEN,
   CWIKI_DURABLE_WRITE_RENAME,
   CWIKI_DURABLE_WRITE_DIRECTORY_SYNC
};

struct cwiki_durable_write_result {
   enum cwiki_durable_write_status status;
   enum cwiki_durable_write_operation operation;
   int error_number;
};

struct cwiki_durable_write_result cwiki_durable_write(const char *path,
    const void *bytes, size_t length);

#ifdef CWIKI_DURABLE_WRITE_TESTING
struct cwiki_durable_write_test_fault {
   enum cwiki_durable_write_operation operation;
   size_t occurrence;
   int error_number;
   size_t write_limit;
};

struct cwiki_durable_write_result cwiki_durable_write_test(const char *path,
    const void *bytes, size_t length,
    const struct cwiki_durable_write_test_fault *fault);
#endif

#endif
