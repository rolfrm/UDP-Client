#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h> //chdir
#include <time.h> //difftime
#include <fcntl.h>
#include <stdint.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/utils.h>
#include <iron/time.h>
#include <iron/fileio.h>
#include <iron/mem.h>
#include <termios.h>
#include <signal.h>
#include <pthread.h>

#include "udpc_share_log.h"

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

typedef enum {
  MANAGER_NONE = 0,
  MANAGER_COMMAND,
  MANAGER_RESPONSE,
  MANAGER_DOWNLOAD_STATUS,
  MANAGER_UPLOAD_STATUS
}log_item_type;

typedef struct {
  log_item_type type;
  union{
    char * command;
    char * response;
    struct{
      char * file;
      int done_percentage;
    }file_progress;
  };
  
}log_item;

void handle_alarm(int signum){
  UNUSED(signum);
  //write(STDIN_FILENO,"\r\n", 2);
  //logd("caught alarm..\n");
  //raise(SIGINT);
}

void handle_int(int signum){
  UNUSED(signum);
  
}

void push_log_item(log_item * items, size_t item_cnt, log_item new_item){
  bool is_download = false, is_upload = false;
  if((is_download = new_item.type == MANAGER_DOWNLOAD_STATUS)
     || (is_upload = new_item.type == MANAGER_UPLOAD_STATUS)){
    for(size_t i = 0; i < item_cnt; i++){
      size_t j = item_cnt - i - 1;
      if(items[j].type == MANAGER_DOWNLOAD_STATUS || items[j].type == MANAGER_UPLOAD_STATUS){
	if(strcmp(items[j].file_progress.file, new_item.file_progress.file) == 0){
	  items[j].file_progress.done_percentage = new_item.file_progress.done_percentage;
	  return;
	}
      }
    }
  }
  if(items[0].type != MANAGER_NONE){
    free(items[0].command);
  }
  
  for(size_t i = 1; i < item_cnt; i++)
    items[i - 1] = items[i];
  items[item_cnt - 1] = new_item;
}

void print_item(log_item item){
  switch(item.type){
  case MANAGER_NONE: return;
  case MANAGER_COMMAND:
    if(item.command == NULL)
      return;
    printf(">> %s", item.command);
    return;
  case MANAGER_RESPONSE:
    if(item.response == NULL)
      return;
    printf("<< %s", item.command);
    return;
  case MANAGER_DOWNLOAD_STATUS:
    if(item.response == NULL)
      return;
    printf("Download: %s %i\%", item.file_progress.file, item.file_progress.done_percentage);
    return;
  case MANAGER_UPLOAD_STATUS:
    if(item.response == NULL)
      return;
    printf("Upload: %s %i\%", item.file_progress.file, item.file_progress.done_percentage);
    return;
  }
}

log_item share_log_item_to_item(share_log_item item, log_item last){
  log_item itm;
  switch(item.type){
  case SHARE_LOG_PROGRESS:
    {
      int percentage = item.progress.total_bytes == 0 ? 100 : ((100 * item.progress.bytes_handled) / item.progress.total_bytes);
      itm = last;
      itm.file_progress.done_percentage = percentage;
      return itm;
    }
  case SHARE_LOG_START_SEND:
  case SHARE_LOG_START_RECEIVE:
    itm.file_progress.file = fmtstr("%s", item.file_name);
    itm.file_progress.done_percentage = 0;
    if(item.type == SHARE_LOG_START_SEND){
      itm.type = MANAGER_UPLOAD_STATUS;
    }else{
      itm.type = MANAGER_DOWNLOAD_STATUS;
    }
    
    return itm;
  default:
    break;
  }
  return last;
}

typedef struct{
  log_item items[10];
}manager_ctx;

void handle_command(manager_ctx * ctx, char * command){
  UNUSED(ctx);
  UNUSED(command);
  log_item newitem;
  newitem.type = MANAGER_COMMAND;
  newitem.command = command;


  if(string_startswith(command, "add ")){

  }else if(string_startswith(command, "remove")){
      
  }else if(string_startswith(command, "help")){
	
  }else{
    newitem.command = fmtstr("Unknown command '%s'", command);
    free(command);
  }
  push_log_item(ctx->items, array_count(ctx->items), newitem);      
      
}

int main(int argc, char ** argv){
  ASSERT(argc == 2);
  char * share_name = argv[1];
  logd("Share: %s\n", share_name);
  char buff[100] = {0};
  char * buffer = buff;
  manager_ctx ctx = {0};

  pthread_t tid;
  pthread_t maintrd = pthread_self();
  share_log_reader * reader = share_log_open_reader(argv[1]);
  void * read_keys(void * userdata){
    UNUSED(userdata);
    while(true){
      char ch = getchar();
      
      if(ch == 127){
	*buffer = 0;
	if(buffer != buff){
	  buffer = buffer - 1;
	}
	
	*buffer = 0;
      }else{
	*buffer = ch;
	buffer++;
      }
      pthread_kill(maintrd, SIGUSR1);
    }
    return NULL;
  }

  pthread_create( &tid, NULL, read_keys, buffer);
  signal(SIGUSR1,handle_alarm);
  printf(" \033[7l\r");
  
  for(size_t i = 0; i < array_count(ctx.items)+2; i++)
    printf("\n");
  while(true){
    share_log_item items2[10];
    int read_items;
    log_item last_item = {0};
    while(0 != (read_items = share_log_reader_read(reader, items2, array_count(items2)))){
      for(int i = 0; i <read_items; i++){
	last_item = share_log_item_to_item(items2[i], last_item);
	push_log_item(ctx.items, array_count(ctx.items), last_item);
      }
    }

    printf("\033[%iA", array_count(ctx.items));
    printf("\033[J");
    for(size_t j = 0; j < array_count(ctx.items); j++){
      printf("\033[2k\r");

      printf(" \033[30D\r");
      print_item(ctx.items[j]);
      printf("\n");
    }

    printf("\r\033[2k");
    fflush(stdout);
    printf(">>>");
    printf("%s", buff);
    fflush(stdout);
    sleep(1);
    
    for(char * ptr = buff; ptr <= buffer; ptr++){
      if(*ptr == '\r' || *ptr =='\n'){
	buffer = buff;
	*ptr = 0;
	char * s = fmtstr("%s", buff);
	handle_command(&ctx, s);
	memset(buff, 0, sizeof(buff));
	break;
      }
    }
  }
  

  return 0;
}
