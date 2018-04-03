#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <iron/log.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <iron/process.h>
#include <iron/mem.h>
#include <udpc.h>
#include "orbital.h"
#include "bloom.h"
#include "murmurhash2.h"
#include <dirent.h>
#include <ftw.h>
#include <icydb.h>

data_log_null null_item = {.header = {.file_id = 0, .type = DATA_LOG_NULL}};

void data_log_generate_items(const char * directory, void (* f)(const data_log_item_header * item, void * userdata), void * userdata){
  struct bloom bloom;
  bloom_init(&bloom, 1000000, 0.01);
  void yield(data_log_item_header * header){
    f(header, userdata);
  }

  yield((data_log_item_header *) &null_item);
  const char * fst = NULL;
  int fstlen = 0;

  u64 gethash(const char * name, u64 prev){
    u64 out;
    u32 * hsh = (u32 *) &out;
    hsh[0] = murmurhash2(name, strlen(name), (u32)prev);
    hsh[1] = murmurhash2(name, strlen(name), (u32)(prev + 0xFFFF));
    return out;
  }

  u64 getid(const char * name){
    u64 mm = gethash(name, 0);
    while(bloom_check(&bloom, &mm, sizeof(mm))){
      mm = gethash(name, mm);
    }
    bloom_add(&bloom, &mm, sizeof(mm));
    return mm;
  }
  
  int lookup (const char * name, const struct stat64 * stati, int flags){
    void emit_name(u64 id){
      data_log_name n = {.header = {.file_id = id, .type = DATA_LOG_NAME}, .name = name};
      yield((data_log_item_header *) &n);
    }
    if(fst == NULL){
      fst = name;
      fstlen = strlen(fst);
      return 0;
    }
    UNUSED(stati);
    UNUSED(flags);
    logd("NAme: %s %i\n", name + fstlen + 1, flags);

    bool is_file = flags == 0;
    bool is_dir = flags == 1;
    if(is_file){
      data_log_new_file f1 = { .size = stati->st_size};
      FILE * f = fopen(name, "r");
      if(f == NULL){
	return 0;
      }
      f1.header.file_id = getid(name + fstlen + 1);
      f1.header.type = DATA_LOG_NEW_FILE;
      yield((data_log_item_header *) &f1);
      emit_name(f1.header.file_id);

      u8 buffer[1024 * 4];
      u32 read = 0;
      while((read = fread(buffer, 1, sizeof(buffer), f))){
	data_log_data data = {
	  .header = {.file_id = f1.header.file_id, .type = DATA_LOG_DATA},
	  .offset = ftell(f),
	  .size = read,
	  .data = buffer};
	yield(&data.header);
      }
      fclose(f);
    }
    else if(is_dir){
      data_log_new_dir d;
      d.header.file_id = getid(name + fstlen + 1);
      d.header.type = DATA_LOG_NEW_DIR;
      yield((data_log_item_header *) &d);
      emit_name(d.header.file_id);
    }
    
    return 0;
  }
  ftw64(directory, lookup, 10000);
  bloom_free(&bloom);
}

typedef char * string;

#include "u64_ptr.h"
#include "u64_ptr.c"


typedef struct{
  u64_ptr * id_name_lookup;
  struct bloom bloom;
  FILE * commits_file;
}datalog_internal;

void datalog_initialize(datalog * dlog, const char * root_dir, const char * commits_file){
  ASSERT(dlog != NULL);
  ASSERT(root_dir != NULL);
  ASSERT(commits_file != NULL);
  dlog->root = fmtstr("%s", root_dir);
  dlog->commits_file = fmtstr("%s", commits_file);
  datalog_internal * dlog_i = alloc(sizeof(datalog_internal));
  dlog_i->id_name_lookup = u64_ptr_create(NULL);
  bloom_init(&dlog_i->bloom, 1000000, 0.01);
  dlog_i->commits_file = NULL;//fopen(commits_file, "r+");
  dlog->internal = dlog_i;
}

static size_t item_size(const data_log_item_header * item){
  switch(item->type){
  case DATA_LOG_NEW_FILE:
    return sizeof(data_log_new_file);
  case DATA_LOG_NEW_DIR:
    return sizeof(data_log_new_dir);
  case DATA_LOG_NULL:
    return sizeof(data_log_null);
  case DATA_LOG_DELETED:
    return sizeof(data_log_deleted);
  default:
    ERROR("Invalid operation");
    return 0;
  }
}

void handle_item_init(const data_log_item_header * item, void * userdata){

  datalog * dlog = userdata;
  UNUSED(dlog);
  switch(item->type){
  case DATA_LOG_NEW_FILE:
  case DATA_LOG_NEW_DIR:
  case DATA_LOG_NULL:
  case DATA_LOG_DELETED:
    {
      size_t s = item_size(item);
      logd("S: %i\n", s);
      break;
    };
  default:
    break;
  }
}

void datalog_update(datalog * dlog){
  datalog_internal * dlog_i = dlog->internal;
  if(dlog_i->commits_file == NULL){
    dlog_i->commits_file = fopen(dlog->commits_file, "r+");

    data_log_generate_items(dlog->root, handle_item_init, dlog);
    return;
  }
}

void datalog_destroy(datalog ** dlog){
  UNUSED(dlog);
  ASSERT(false);
}
