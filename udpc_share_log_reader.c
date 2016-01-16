#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <iron/types.h>
#include <iron/utils.h>
#include <iron/log.h>

#include "udpc_share_log.h"

#include <stdarg.h>
void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  iron_log_stacktrace();
  raise(SIGSTOP);
  exit(255);
}


int main(int argc, char ** argv){
  if(argc != 2){
    ERROR("'%s' requires exactly one argument, argv[0]");
  }
  share_log_reader * reader = share_log_open_reader(argv[1]);
  if(reader == NULL){
    ERROR("Unable to read file %s\n", argv[1]);
  }
  share_log_item items[10];
  int offset = 0;
  while(true){
    int read_items = share_log_reader_read(reader, items, array_count(items));
    logd("OK %i\n", read_items);
    if(read_items == 0)
      break;
    logd("Read %i items\n", read_items);
    for(int i = 0; i < read_items; i++){
      logd("Item %i: ", offset++);
      share_log_item_print(items[i]);
      logd("\n");
    }
    share_log_clear_items(items, read_items);
  }
  
  share_log_close_reader(&reader);
  
  return 0;

}
