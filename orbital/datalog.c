#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

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
#include <xxhash.h>

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
      data_log_name n = {.header = {.file_id = id, .type = DATA_LOG_NAME}, .name = name + fstlen + 1};
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

typedef struct{
  const char * name;
  u64 size;
  data_log_timestamp last_edit;
  bool is_dir;
}datalog_item_data;

#include "u64_dlog.h"
#include "u64_dlog.c"


typedef struct{
  u64_dlog * id_name_lookup;
  struct bloom bloom;
  FILE * commits_file;
  FILE * datalog_file;
  char prevhash[8];
  XXH64_state_t*  state;
}datalog_internal;



void datalog_initialize(datalog * dlog, const char * root_dir, const char * datalog_file, const char * commits_file){
  ASSERT(dlog != NULL);
  ASSERT(root_dir != NULL);
  ASSERT(commits_file != NULL);
  dlog->root = fmtstr("%s", root_dir);
  dlog->commits_file = fmtstr("%s", commits_file);
  dlog->datalog_file = fmtstr("%s", datalog_file);
  datalog_internal * dlog_i = alloc(sizeof(datalog_internal));
  dlog_i->state = XXH64_createState();
  dlog_i->id_name_lookup = u64_dlog_create(NULL);
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

typedef struct{
  u64 hash;
  u32 length;
}commit_item;

void handle_item_init(const data_log_item_header * item, void * userdata){

  datalog * dlog = userdata;
  datalog_internal * dlog_i = dlog->internal;
  XXH64_reset(dlog_i->state, 0);
  u32 totallen = 0;
  void write(const void * data, size_t len){
    fwrite(data, len, 1, dlog_i->datalog_file);
    XXH64_update(dlog_i->state, data, len);
    totallen += (u32)len;
  }
  
  switch(item->type){
  case DATA_LOG_NEW_FILE:
  case DATA_LOG_NEW_DIR:
  case DATA_LOG_NULL:
  case DATA_LOG_DELETED:
    {
      size_t s = item_size(item);
      logd("S: %i\n", s);
      fwrite(item, s, 1, dlog_i->datalog_file);
      XXH64_update(dlog_i->state, item, s);
      totallen = s;
      break;
    };
  case DATA_LOG_NAME:
    {
      data_log_name * dlogname = (data_log_name *) item;
      size_t s = sizeof(data_log_name) - sizeof(dlogname->name);
      write(dlogname, s);
      size_t len = strlen(dlogname->name);
      write(&len, sizeof(len));      
      write(dlogname->name, len);
      break;
    }
  case DATA_LOG_DATA:
    {
      data_log_data * dlogdata = (data_log_data *) item;
      size_t s = sizeof(data_log_data) - sizeof(dlogdata->data);
      write(dlogdata, s);
      write(dlogdata->data, dlogdata->size);
      break;
    }
  default:
    break;
  }
  unsigned long long const hash = XXH64_digest(dlog_i->state);
  commit_item ci;
  ci.hash = hash;
  ci.length = totallen;
  fwrite(&ci, sizeof(ci), 1, dlog_i->commits_file);
  
}
void datalog_apply_item(datalog * dlog, const data_log_item_header * item, bool register_only);

void datalog_update(datalog * dlog){
  datalog_internal * dlog_i = dlog->internal;
  if(dlog_i->commits_file == NULL){
    dlog_i->commits_file = fopen(dlog->commits_file, "w+");
    dlog_i->datalog_file = fopen(dlog->datalog_file, "w+");
    u64 cnt = datalog_get_commit_count(dlog);
    if(cnt == 0){
      data_log_generate_items(dlog->root, handle_item_init, dlog); 
      return;
    }
    datalog_iterator it = datalog_iterator_create(dlog);
    const data_log_item_header * nxt = NULL;
    while((nxt = datalog_iterator_next(&it)) != NULL){
      datalog_apply_item(dlog, nxt, true);
    }
    datalog_iterator_destroy(&it);
  }
  
}

void datalog_destroy(datalog ** dlog){
  UNUSED(dlog);
  ASSERT(false);
}

typedef struct{
  FILE * commits_file;
  FILE * datalog_file;
  void * buffer;
  size_t buffer_size;
  void * buffer2;
  size_t buffer2_size;
  
}datalog_iterator_internal;

size_t stream_length(FILE * f){
  size_t pos = ftell(f);
  fseek(f, 0, SEEK_END);
  size_t end = ftell(f);
  fseek(f, pos, SEEK_SET);
  return end;
}

datalog_iterator datalog_iterator_create(datalog * dlog){
  datalog_iterator it;
  it.dlog = dlog;
  it.head = NULL;
  it.offset = 0;
  it.commit_index = 0;

  datalog_iterator_internal iti = {0};
  datalog_internal * dlog_i = dlog->internal;
  fflush(dlog_i->commits_file);
  fflush(dlog_i->datalog_file);
  iti.commits_file = fopen(dlog->commits_file, "r");
  iti.datalog_file = fopen(dlog->datalog_file, "r");

  it.internal = IRON_CLONE(iti);
  
  return it;
}

const data_log_item_header * datalog_iterator_next(datalog_iterator * it){
  datalog_iterator_internal * iti = it->internal;
  if(iti == NULL)
    return NULL;
  commit_item ci;
  logd("%i/%i %i/%i\n", ftell(iti->commits_file),stream_length(iti->commits_file), ftell(iti->datalog_file), stream_length(iti->datalog_file));
  ASSERT(stream_length(iti->commits_file) > 0);
  ASSERT(stream_length(iti->datalog_file) > 0);
  int read = fread(&ci, sizeof(ci), 1, iti->commits_file);

  if(read ==  1){
    if(iti->buffer_size < ci.length)
      iti->buffer = ralloc(iti->buffer, (iti->buffer_size = ci.length));
    logd("len: %i\n", ci.length);
    ASSERT(fread(iti->buffer, ci.length, 1, iti->datalog_file) > 0);
    data_log_item_header * hd = iti->buffer;
    if(hd->type == DATA_LOG_NAME){
      size_t * l = iti->buffer + sizeof(data_log_name) - sizeof(void *);
      size_t strlen = *l;
      char * str = iti->buffer + sizeof(data_log_name) - sizeof(void *) + sizeof(*l);
      size_t s2 = sizeof(data_log_name) + strlen + 1;
      if(iti->buffer2_size < s2)
	iti->buffer2 = ralloc(iti->buffer2, (iti->buffer2_size = s2));
      data_log_name * dname = iti->buffer2;
      *dname = ((data_log_name *) iti->buffer)[0];
      memcpy(iti->buffer2 + sizeof(data_log_name), str, *l);
      char * _str = iti->buffer2 + sizeof(data_log_name);
      _str[*l] = 0;
      dname->name = _str;
      
      return iti->buffer2;
    }else if(hd->type == DATA_LOG_DATA){
      void * data = iti->buffer + sizeof(data_log_data) - sizeof(void *);
      var dl = ((data_log_data *) iti->buffer)[0];
      dl.data = data;
      
      if(iti->buffer2_size < sizeof(data_log_data))
	iti->buffer2 = ralloc(iti->buffer2, (iti->buffer2_size = sizeof(data_log_data)));
      ((data_log_data *) iti->buffer2)[0] = dl;
      return iti->buffer2;
    }else{
      return iti->buffer;
    }
    
  }else{
    datalog_iterator_destroy(it);
    return NULL;
  }
}
void datalog_iterator_destroy(datalog_iterator * it){
  datalog_iterator_internal * iti = it->internal;
  if(iti == NULL) return;
  dealloc(iti->buffer);
  dealloc(iti->buffer2);
  fclose(iti->commits_file);
  fclose(iti->datalog_file);
  dealloc(iti);
  it->internal = NULL;
}

static char * translate_dir(datalog * dlog, const char * path){
  static __thread char buffer[1000];
  sprintf(buffer, "%s/%s", dlog->root, path);
  return buffer;
}

void datalog_apply_item(datalog * dlog, const data_log_item_header * item, bool register_only){
  datalog_internal * dlog_i = dlog->internal;
  if(!register_only)
    handle_item_init(item, dlog);
  switch(item->type)
    {
    case DATA_LOG_NEW_FILE:
      {
	
	data_log_new_file * f = (data_log_new_file *) item;
	datalog_item_data d = {0};
	d.last_edit = f->last_edit;
	d.size = f->size;
	u64_dlog_set(dlog_i->id_name_lookup, item->file_id, d);
	break;
      }
    case DATA_LOG_NEW_DIR:
      {
	datalog_item_data d = {0};
	d.is_dir = true;
	u64_dlog_set(dlog_i->id_name_lookup, item->file_id, d);
	break;
      }
    case DATA_LOG_NAME:
      {
	datalog_item_data d;

	data_log_name * f = (data_log_name *) item;
               
	ASSERT(u64_dlog_try_get(dlog_i->id_name_lookup, (void *)&item->file_id, &d));
	d.name = fmtstr("%s", f->name);
	u64_dlog_set(dlog_i->id_name_lookup, item->file_id, d);
	if(d.is_dir){
	  char * buf = translate_dir(dlog, d.name);
	  if(!register_only)
	    mkdir(buf, S_IRWXU);
	}
      
	break;
      }
    case DATA_LOG_DATA:
      {
	datalog_item_data d;
	ASSERT(u64_dlog_try_get(dlog_i->id_name_lookup, (void *) &item->file_id, &d));

	if(!register_only){
	  data_log_data * da = ((data_log_data *) item);
	  char * buf = translate_dir(dlog, d.name);
	  FILE * file = fopen(buf, "w");
	  ASSERT(file);
	  fseek(file, da->offset, SEEK_SET);
	  fwrite(da->data, 1, da->size, file);
	  fclose(file);
	}
	break;
      
      }
    case DATA_LOG_DELETED:
      {

	datalog_item_data d;
	ASSERT(u64_dlog_try_get(dlog_i->id_name_lookup,  (void *)&item->file_id, &d));
	if(!register_only){
	  char * buf = translate_dir(dlog, d.name);
	  if(d.is_dir){
	    rmdir(buf);
	  }else{
	    remove(buf);
	  }
	}
	break;
      }
    case DATA_LOG_NULL:
      break;
    }
}


u64 datalog_get_commit_count(datalog * dlog){
  datalog_internal * dlog_i = dlog->internal;
  var opos = ftell(dlog_i->commits_file);
  fseek(dlog_i->commits_file, 0, SEEK_END);
  var epos = ftell(dlog_i->commits_file);
  fseek(dlog_i->commits_file, opos, SEEK_SET);
  return epos / sizeof(commit_item);
}
