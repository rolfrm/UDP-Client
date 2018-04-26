#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

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
u64 get_file_time(const struct stat64 * stati);
void set_file_time(const char * file, u64 stamp);

typedef struct{
  const char * name;
  u64 size;
  data_log_timestamp last_edit;
  bool is_dir;
}datalog_item_data;

#include "u64_dlog.h"
#include "u64_dlog.c"

typedef struct {
  struct bloom * bloom;
  u64_dlog * id_name_lookup;
}datalog_item_generator;

void datalog_generate_items2(datalog_item_generator * gen, const char * directory, void (* f)(const data_log_item_header * item, void * userdata), void * userdata){

  void yield(data_log_item_header * header){
    f(header, userdata);
  }
  u64 null_id = null_item.header.file_id;
  if(!bloom_check(gen->bloom, &null_id, sizeof(null_id))){
    yield((data_log_item_header *) &null_item);
    bloom_add(gen->bloom, &null_id, sizeof(null_id));
  }
  const char * fst = NULL;
  int fstlen = 0;
  u64 gethash(const char * name, u64 prev){
    u64 out;
    u32 * hsh = (u32 *) &out;
    hsh[0] = murmurhash2(name, strlen(name), (u32)prev);
    hsh[1] = murmurhash2(name, strlen(name), (u32)(prev + 0xFFFF));
    return out;
  }

  u64 getid(const char * name, datalog_item_data * d){

    u64 mm = gethash(name, 0);
    while(bloom_check(gen->bloom, &mm, sizeof(mm))){
      // this could be a collision or because the file already exists.
      // if it already exists, then just return the id.
      if(gen->id_name_lookup != NULL){
	if(u64_dlog_try_get(gen->id_name_lookup, &mm, d)){
	  if(strcmp(d->name, name) == 0)
	    return mm;
	}
      }
      mm = gethash(name, mm);
    }
    bloom_add(gen->bloom, &mm, sizeof(mm));
    return mm;
  }
  
  int lookup (const char * name, const struct stat64 * stati, int flags){
    void emit_name(u64 id){
      data_log_name n = {.header = {.file_id = id, .type = DATA_LOG_NAME}, .name = name + fstlen + 1};
      yield(&n.header);
    }
    if(fst == NULL){
      fst = name;
      fstlen = strlen(fst);
      return 0;
    }

    bool is_file = flags == 0;
    bool is_dir = flags == 1;
    if(is_file){
      data_log_new_file f1 = { .size = stati->st_size,.last_edit = get_file_time(stati)};
      datalog_item_data d = {0};
      f1.header.file_id = getid(name + fstlen + 1, &d);
      if(d.name != NULL){
	// if its refound, only care if its newer than the old one.
	if(d.last_edit >= f1.last_edit && d.size == f1.size)
	  return 0;
      }
      FILE * f = fopen(name, "r");
      if(f == NULL){
	return 0;
      }
      f1.header.type = DATA_LOG_NEW_FILE;
      yield(&f1.header);
      emit_name(f1.header.file_id);

      u8 buffer[1024 * 1];
      u32 read = 0;
      while((read = fread(buffer, 1, sizeof(buffer), f))){
	data_log_data data = {
	  .header = {.file_id = f1.header.file_id, .type = DATA_LOG_DATA},
	  .offset = ftell(f) - read,
	  .size = read,
	  .data = buffer};
	yield(&data.header);
      }
      fclose(f);
    }
    else if(is_dir){
      data_log_new_dir d;
      datalog_item_data d2 = {0};
      d.header.file_id = getid(name + fstlen + 1, &d2);
      if(d2.name != NULL) return 0;
      d.header.type = DATA_LOG_NEW_DIR;
      yield((data_log_item_header *) &d);
      emit_name(d.header.file_id);
    }
    
    return 0;
  }
  ftw64(directory, lookup, 10000);
}


void data_log_generate_items(const char * directory, void (* f)(const data_log_item_header * item, void * userdata), void * userdata){

  struct bloom bloom;
  bloom_init(&bloom, 1000000, 0.01);
  datalog_item_generator gen = {0};
  gen.bloom = &bloom;
  datalog_generate_items2(&gen, directory, f, userdata);
  bloom_free(&bloom);
}

typedef struct{
  u64_dlog * id_name_lookup;
  struct bloom bloom;
  FILE * commits_file;
  FILE * datalog_file;
  char prevhash[8];
  XXH64_state_t*  state;

  void * write_buffer;
  size_t write_buffer_size;
  
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
  dlog_i->write_buffer = NULL;
  dlog_i->write_buffer_size = 0;
  bloom_init(&dlog_i->bloom, 1000000, 0.01);
  dlog_i->commits_file = NULL;
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

size_t datalog_cpy_to_buffer(const data_log_item_header * item, void ** buffer, size_t * buffer_size){
  u64 offset = 0;
  void write(const void * data, size_t len){
    if(len + offset > *buffer_size)
      *buffer = ralloc(*buffer, (*buffer_size = (len + offset)));
    memcpy(*buffer + offset, data, len);
    offset += len;
  }
  
  switch(item->type){
  case DATA_LOG_NEW_FILE:
  case DATA_LOG_NEW_DIR:
  case DATA_LOG_NULL:

    // fall through
  case DATA_LOG_DELETED:
    {
      size_t s = item_size(item);
      write(item, s);
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
  return offset;
}

void datalog_cpy_to_file(datalog * dlog, const data_log_item_header * item, FILE * file){
  datalog_internal * dlog_i = dlog->internal;
  u64 len = datalog_cpy_to_buffer(item, &dlog_i->write_buffer, &dlog_i->write_buffer_size);
  fwrite(dlog_i->write_buffer, len, 1, file);
}

void handle_item_init(const data_log_item_header * item, void * userdata){

  datalog * dlog = userdata;
  datalog_internal * dlog_i = dlog->internal;

  u64 len = datalog_cpy_to_buffer(item, &dlog_i->write_buffer, &dlog_i->write_buffer_size);
  fwrite(dlog_i->write_buffer, len, 1, dlog_i->datalog_file);
  XXH64_reset(dlog_i->state, 0);
  XXH64_update(dlog_i->state, dlog_i->write_buffer, len);
  
  unsigned long long const hash = XXH64_digest(dlog_i->state);
  commit_item ci;
  ci.hash = hash;
  ci.length = len;
  fwrite(&ci, sizeof(ci), 1, dlog_i->commits_file);
  
}
void datalog_apply_item(datalog * dlog, const data_log_item_header * item, bool register_only, bool write_commit);

void datalog_update(datalog * dlog){
  datalog_internal * dlog_i = dlog->internal;

  if(dlog_i->commits_file == NULL){
    dlog_i->commits_file = fopen(dlog->commits_file, "r+");
    if(dlog_i->commits_file == NULL)
      dlog_i->commits_file = fopen(dlog->commits_file, "w+");
    dlog_i->datalog_file = fopen(dlog->datalog_file, "r+");
    if(dlog_i->datalog_file == NULL)
      dlog_i->datalog_file = fopen(dlog->datalog_file, "w+");
    u64 cnt = datalog_get_commit_count(dlog);
    if(cnt != 0){
      datalog_iterator it;
      datalog_iterator_init(dlog, &it);
      const data_log_item_header * nxt = NULL;
      while((nxt = datalog_iterator_next(&it)) != NULL)
	datalog_apply_item(dlog, nxt, true, false);
      
      datalog_iterator_destroy(&it);
      fseek(dlog_i->commits_file, 0, SEEK_END);
      fseek(dlog_i->datalog_file, 0, SEEK_END);

    }
  }

  datalog_item_generator gen = {0};
  gen.bloom = &dlog_i->bloom;
  gen.id_name_lookup = dlog_i->id_name_lookup;
  
  void add_item(const data_log_item_header * item, void * userdata){

    datalog_apply_item((datalog *) userdata, item, true, true);
  }
  
  datalog_generate_items2(&gen, dlog->root, add_item, dlog);
}

void datalog_destroy(datalog * dlog){
  ASSERT(dlog->internal != NULL);
  datalog_internal * dlog_i = dlog->internal;
  if(dlog_i->commits_file != NULL){
    fclose(dlog_i->commits_file);
    fclose(dlog_i->datalog_file);
  }
  dealloc(dlog_i);
  dlog->internal = NULL;
}

typedef struct{
  FILE * commits_file;
  FILE * datalog_file;
  void * buffer;
  size_t buffer_size;
  
}datalog_iterator_internal;

size_t stream_length(FILE * f){
  size_t pos = ftell(f);
  fseek(f, 0, SEEK_END);
  size_t end = ftell(f);
  fseek(f, pos, SEEK_SET);
  return end;
}

void datalog_iterator_init2(datalog * dlog, datalog_iterator * it, u64 commit_offset, u64 commit_index){
  datalog_iterator_init(dlog, it);
  var it2 = (datalog_iterator_internal*) it->internal;
  fseek(it2->commits_file, -commit_index * sizeof(commit_item), SEEK_END);
  fseek(it2->datalog_file, -commit_offset, SEEK_END);
}

void datalog_iterator_init(datalog * dlog, datalog_iterator * it){
  
  it->dlog = dlog;
  it->head = NULL;
  it->offset = 0;
  it->commit_index = 0;

  datalog_iterator_internal iti = {0};
  datalog_internal * dlog_i = dlog->internal;
  fflush(dlog_i->commits_file);
  fflush(dlog_i->datalog_file);
  iti.commits_file = fopen(dlog->commits_file, "r");
  iti.datalog_file = fopen(dlog->datalog_file, "r");

  it->internal = IRON_CLONE(iti);
}

const data_log_item_header * datalog_iterator_next0(FILE * f, void ** buffer, size_t * buffer_size){

  int read(void * out, size_t len){
    return fread(out, len, 1, f);
  }
  
  if(*buffer_size < sizeof(data_log_item_header))
    *buffer = ralloc(*buffer, (*buffer_size = sizeof(data_log_item_header) * 4));
  
  if(read(*buffer, sizeof(data_log_item_header)) < 1)
    return NULL;
  data_log_item_header * hd = *buffer;

  size_t s;
  
  if(hd->type == DATA_LOG_NAME){
    s = sizeof(data_log_name) - sizeof(void *);
  }else if(hd->type == DATA_LOG_DATA){
    s = sizeof(data_log_data) - sizeof(void *);;
  }else{
    s = item_size(hd);
  }
  size_t rest = s - sizeof(data_log_item_header);
  if(*buffer_size < s)
    *buffer = ralloc(*buffer, (*buffer_size = s * 4));
  
  if(rest > 0)
    ASSERT(read(*buffer + sizeof(data_log_item_header), rest) > 0);
  
  if(hd->type == DATA_LOG_NAME){
    size_t l = 0;
    read(&l, sizeof(l));
    if(*buffer_size < sizeof(data_log_name) + l + 1)
      *buffer = ralloc(*buffer, *buffer_size = (sizeof(data_log_name) + l + 1));
    read(*buffer + sizeof(data_log_name), l);
    data_log_name * dl = *buffer;
    char * name =*buffer + sizeof(data_log_name); 
    dl->name = name;
    name[l] = 0;
  }else if(hd->type == DATA_LOG_DATA){
    data_log_data * dld = *buffer;
    size_t l = dld->size;
    if(*buffer_size < sizeof(data_log_data) + l){
      *buffer = ralloc(*buffer, *buffer_size = (sizeof(data_log_data) + l));
      dld = *buffer;
    }
    read(*buffer + sizeof(data_log_data), l);
    
    dld->data = *buffer + sizeof(data_log_data);
  }
  

  return *buffer;
  
}

const data_log_item_header * datalog_iterator_next(datalog_iterator * it){

  // TODO: Change it so that datalog_iterator does not need to read from the commits file
  // this makes it complicated to save partial states.

  datalog_iterator_internal * iti = it->internal;
  if(iti == NULL)
    return NULL;
  commit_item ci;
  ASSERT(stream_length(iti->commits_file) > 0);
  ASSERT(stream_length(iti->datalog_file) > 0);
  int read = fread(&ci, sizeof(ci), 1, iti->commits_file);
  
  var hd =  datalog_iterator_next0(iti->datalog_file, &iti->buffer, &iti->buffer_size);
  if(hd == NULL)
    ASSERT(read == 0);
  if(hd == NULL)
    datalog_iterator_destroy(it);
  return hd;
}

void datalog_iterator_destroy(datalog_iterator * it){
  datalog_iterator_internal * iti = it->internal;
  if(iti == NULL) return;
  dealloc(iti->buffer);
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

void datalog_apply_item(datalog * dlog, const data_log_item_header * item, bool register_only, bool write_commit){
  datalog_internal * dlog_i = dlog->internal;
  if(write_commit)
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
	bloom_add(&dlog_i->bloom, &item->file_id, sizeof(item->file_id));
	break;
      }
    case DATA_LOG_NEW_DIR:
      {
	datalog_item_data d = {0};
	d.is_dir = true;
	u64_dlog_set(dlog_i->id_name_lookup, item->file_id, d);
	bloom_add(&dlog_i->bloom, &item->file_id, sizeof(item->file_id));
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
	bloom_add(&dlog_i->bloom, &item->file_id, sizeof(item->file_id));
      
	break;
      }
    case DATA_LOG_DATA:
      {
	datalog_item_data d;
	ASSERT(u64_dlog_try_get(dlog_i->id_name_lookup, (void *) &item->file_id, &d));

	if(!register_only){
	  data_log_data * da = ((data_log_data *) item);
	  char * buf = translate_dir(dlog, d.name);
	  FILE * file = fopen(buf, "r+");
	  if(file == NULL)
	    file = fopen(buf, "w+");
	  ASSERT(file);
	  fseek(file, da->offset, SEEK_SET);
	  ASSERT(strcmp(dlog->root, "sync_1") != 0);
	  fwrite(da->data, da->size, 1, file);
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
      bloom_add(&dlog_i->bloom, &item->file_id, sizeof(item->file_id));
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

void datalog_update_files(datalog * dlog){
  datalog_internal * dlog_i = dlog->internal;
  for(size_t i = 0; i < dlog_i->id_name_lookup->count; i++){
    datalog_item_data * item = dlog_i->id_name_lookup->value + i + 1;
    if(item->is_dir || item->name == NULL) continue;
    char * fpath = translate_dir(dlog, item->name);
    ASSERT(truncate64(fpath, item->size) == 0);
    set_file_time(fpath, item->last_edit); 
  }
}

typedef struct{
  FILE * file;
}datalog_commit_iterator_internal;

void datalog_commit_iterator_init(datalog_commit_iterator * it, datalog * dlog, bool reverse){
  it->dlog = dlog;
  it->reverse = reverse;
  datalog_commit_iterator_internal * internal = it->internal = alloc(sizeof(datalog_commit_iterator_internal));
  internal->file = fopen(dlog->commits_file, "r");
  if(internal->file == NULL){
    dealloc(internal);
    it->internal = NULL;
    return;
  }
  if(reverse)
    fseek(internal->file, -sizeof(commit_item), SEEK_END);
}

void datalog_iterator_init_from(datalog_iterator * it, datalog * dlog, commit_item item){
  datalog_commit_iterator it2;
  datalog_commit_iterator_init(&it2, dlog, true);
  size_t size = 0;
  size_t index =0;
  while(true){
    commit_item _item;
    var ok = datalog_commit_iterator_next(&it2, &_item);
    if(!ok) return;
    if(_item.hash == item.hash && _item.length == item.length){
      break;
    }
    size += _item.length;
    index += 1;
  }
  datalog_iterator_init2(dlog, it, size, index);
}

void datalog_commit_iterator_destroy(datalog_commit_iterator * it){
  ASSERT(it->internal != NULL);
  datalog_commit_iterator_internal * itn = it->internal;
  fclose(itn->file);
  dealloc(itn);
  it->internal = NULL;
}

bool datalog_commit_iterator_next(datalog_commit_iterator * it, commit_item * item){
  datalog_commit_iterator_internal * itn = it->internal;
  if(itn == NULL) return false;
  if(it->reverse){

    if(fread(item,sizeof(item[0]),1,itn->file) == 0)
      goto failure;

    // When the last thing was read fseek is done to a negative pos, which causes it to fail.
    if(fseek(itn->file, -sizeof(item[0]) * 2, SEEK_CUR) != 0)
      datalog_commit_iterator_destroy(it); 
    
    
  }else{
    if(fread(item,sizeof(item[0]),1,itn->file) == 0)
      goto failure;
  }
  return true;

 failure:
  datalog_commit_iterator_destroy(it);
  return false;
}

commit_item datalog_get_commit(datalog * dlog, u64 index){
  datalog_internal * dlog_i = dlog->internal;
  
  u64 cnt = datalog_get_commit_count(dlog);
  ASSERT(index < cnt);
  var opos = ftell(dlog_i->commits_file);
  fseek(dlog_i->commits_file, index * sizeof(commit_item), SEEK_SET);
  commit_item out;
  size_t rcnt = fread(&out, sizeof(commit_item), 1, dlog_i->commits_file);
  ASSERT(1 == rcnt);
  fseek(dlog_i->commits_file, opos, SEEK_SET);
  return out;
}

void datalog_rewind_to(datalog * dlog, datalog_iterator * it){
  datalog_internal * dlog_i = dlog->internal;
  datalog_iterator_internal * iti = it->internal;
  fseek(dlog_i->datalog_file, ftell(iti->datalog_file), SEEK_SET);
  fseek(dlog_i->commits_file, ftell(iti->commits_file), SEEK_SET);
  datalog_iterator_destroy(it);
}

void datalog_generate_from_file(const char * file, void (* fcn)(const data_log_item_header * item, void * userdata), void * userdata){
  FILE * f = fopen(file, "r");
  void * buffer = NULL;
  size_t len = 0;
  const data_log_item_header * hd = NULL;
  while(NULL != (hd = datalog_iterator_next0(f,&buffer, &len))){
    fcn(hd, userdata);
  }
  if(buffer != NULL)
    dealloc(buffer);
  fclose(f);
      
}

