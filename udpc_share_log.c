#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/mem.h>
#include <iron/log.h>
#include <iron/fileio.h>
#include <inttypes.h>
#include "udpc_share_log.h"

static void write_to_log(const char * path, const char * message){

  FILE * f = fopen(path, "a");
  u64 timestampvalue = timestamp();
  char write_buffer[20];
  int written = sprintf(write_buffer, "%" PRId64 " ", timestampvalue);
  fwrite(write_buffer, written, 1, f);
  int buffer_size = strlen(message);
  fwrite(message, buffer_size, 1, f);
  fwrite("\n", 1, 1, f);
  fclose(f);
}

static char log_file_path[255] = {0};

void share_log_set_file(const char * path){
  if(path == NULL)
    log_file_path[0] = 0;
  else{

    sprintf(log_file_path, "%s", path);
    iron_touch(log_file_path);
  }
}

static char write_buffer[10000];
void share_log_start_send_file(const char * file){
  if(log_file_path[0] == 0) return;
  sprintf(write_buffer, "SEND %s", file);
  write_to_log(log_file_path, write_buffer);
}

void share_log_end_send_file(){
  if(log_file_path[0] == 0) return;
  write_to_log(log_file_path, "END");
}

void share_log_start_receive_file(const char * file){
  if(log_file_path[0] == 0) return;
  sprintf(write_buffer, "RECEIVE %s", file);
  write_to_log(log_file_path, write_buffer);
}

void share_log_end_receive_file(){
  if(log_file_path[0] == 0) return;
  share_log_end_send_file();
}

void share_log_progress(i64 bytes_handled, i64 total_bytes){
  if(log_file_path[0] == 0) return;
  sprintf(write_buffer, "PROGRESS %" PRId64 " / %" PRId64 "", bytes_handled, total_bytes);
  write_to_log(log_file_path, write_buffer);
}

struct _share_log_reader {
  FILE * file;
  char * read_buffer;
  size_t read_buffer_size;
};

share_log_reader * share_log_open_reader(const char * path){
  FILE * file = fopen(path, "r");
  if(file == NULL) return NULL;
  share_log_reader * reader = alloc(sizeof(share_log_reader));
  reader->file = file;
  reader->read_buffer_size = 10000;
  reader->read_buffer = alloc(reader->read_buffer_size);

  return reader;
}

void share_log_close_reader(share_log_reader ** reader){
  ASSERT(*reader != NULL);
  fclose((*reader)->file);
  dealloc((*reader)->read_buffer);
  dealloc(*reader);
  *reader = NULL;
}

int share_log_reader_read(share_log_reader * reader, share_log_item * out_items, int max_items){
  int read_items = 0;
  while(max_items > 0){
    char * str = NULL;
    size_t p = ftell(reader->file);
    int line_len = 0;
    if(0 > (line_len = getline(&reader->read_buffer, &reader->read_buffer_size, reader->file))){
      fseek(reader->file, p, SEEK_SET);
      goto panic;
    }
    if(line_len <= 1)
      continue;

    str = reader->read_buffer;
    ASSERT(str != NULL);
    bool isrcv = false, issnd = false;
    share_log_item item;

    int read = sscanf(str, "%"PRId64" ", &item.timestamp);
    if(read <= 0)
      goto panic;
    str = strchr(str, ' ');
    if(str == NULL)
      goto panic;
    str += 1;
    
    
    if((isrcv = string_startswith(str, "RECEIVE")) || (issnd = string_startswith(str, "SEND"))){
      
      if(isrcv){
	item.type = SHARE_LOG_START_RECEIVE;
	str += strlen("RECEIVE ");
      }
      else if(issnd){
	str += strlen("SEND ");
	item.type = SHARE_LOG_START_SEND;
      }else{
	ERROR("Shouldn't happen");
      }
      size_t s = strlen(str);
      item.file_name = iron_clone(str, s);
      item.file_name[s - 1] = 0;
      
    }else if(string_startswith(str, "END")){
      item.type = SHARE_LOG_END;
    }else if(string_startswith(str, "PROGRESS")){
      item.type = SHARE_LOG_PROGRESS;
      ASSERT(0 <= sscanf(str, "PROGRESS %" PRId64 " / %" PRId64 "", &item.progress.bytes_handled, &item.progress.total_bytes));
    }else{
      goto panic;
    }
    *out_items = item;
    read_items++;
    out_items++;
    max_items--;
    continue;
  panic:
    fseek(reader->file, p, SEEK_SET);
    break;
  }
  return read_items;
}

void share_log_item_print(share_log_item item){
  logd(" %" PRId64 " : ", item.timestamp);
  switch(item.type){
  case SHARE_LOG_PROGRESS:
    logd("PROGRESS %i / %i", item.progress.bytes_handled, item.progress.total_bytes);
    break;
  case SHARE_LOG_END:
    logd("END"); break;
  case SHARE_LOG_START_SEND:
    logd("SEND ");
  case SHARE_LOG_START_RECEIVE:
    if(item.type == SHARE_LOG_START_RECEIVE)
      logd("RECEIVE ");
    logd("'%s'",item.file_name);
    break;
  default:
    ERROR("Invalid share log type %i", item.type);
  }
}

void share_log_clear_items(share_log_item * items, int cnt){
  for(int i = 0; i < cnt; i++){
    if(items[i].type == SHARE_LOG_START_RECEIVE || items[i].type == SHARE_LOG_START_SEND)
      dealloc(items[i].file_name);
    items[i].file_name = NULL;
  }
  memset(items, 0, cnt * sizeof(items[0]));
}
