#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include <iron/log.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <iron/process.h>
#include <iron/mem.h>
#include <udpc.h>
#include "orbital.h"
#include <dirent.h>
#include <ftw.h>

typedef u64 data_log_timestamp;

typedef struct {
  data_log_item_header header;
  data_log_timestamp last_edit;
  u64 size;
}data_log_new_file;

typedef struct {
  data_log_item_header header;

}data_log_new_dir;

typedef struct{
  data_log_item_header header;
  char * name;
}data_log_name;

typedef struct{
  data_log_item_header header;
  u64 offset;
  u64 size;
  void * data;
}data_log_data;

typedef struct{
  data_log_item_header header;
}data_log_deleted;

typedef struct {
  data_log_item_header header;
}data_log_null;

data_log_null null_item = {.header = {.file_id = 0, .type = DATA_LOG_NULL}};

//extern int scandir64 (const char *__restrict __dir,
//		      struct dirent64 ***__restrict __namelist,
//		      int (*__selector) (const struct dirent64 *),
//		      int (*__cmp) (const struct dirent64 **,
//				    const struct dirent64 **))
//     __nonnull ((1, 2));

//extern int scandirat (int __dfd, const char *__restrict __dir,
//		      struct dirent ***__restrict __namelist,
//		      int (*__selector) (const struct dirent *),
//		      int (*__cmp) (const struct dirent **,
//
//const struct dirent **))

//extern int scandirat (int __dfd, const char *__restrict __dir,
//		      struct dirent ***__restrict __namelist,
//		      int (*__selector) (const struct dirent *),
//		      int (*__cmp) (const struct dirent **,
//				    const struct dirent **))

void data_log_generate(const char * directory, void (* f)(const data_log_item_header * item, void * userdata), void * userdata){

  f((data_log_item_header *) &null_item, userdata);

  int lookup (const char * name, const struct stat64 * stati, int flags){
    UNUSED(stati);
    UNUSED(flags);
    logd("NAme: %s\n", name);
    return 0;
  }
  ftw64(directory, lookup, 10000);

}
