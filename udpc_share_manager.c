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
#include <iron/array.h>
#include <iron/process.h>
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
  MANAGER_UPLOAD_STATUS,
  MANAGER_DELETE
}log_item_type;

typedef struct {
  log_item_type type;
  union{
    char * command;
    char * response;
    char * file;
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
    printf("Download: %s %i%%", item.file_progress.file, item.file_progress.done_percentage);
    return;
  case MANAGER_UPLOAD_STATUS:
    if(item.response == NULL)
      return;
    printf("Upload: %s %i%%", item.file_progress.file, item.file_progress.done_percentage);
    return;
  case MANAGER_DELETE:
    printf("Delete: %s", item.file);
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
  case SHARE_LOG_END:
    itm = last;
    last.file_progress.done_percentage = 100;
    break;
  case SHARE_LOG_DELETE:
    itm.type = MANAGER_DELETE;
    itm.file = fmtstr("%s", item.file_name);
    last = itm;
  default:
    break;
  }
  return last;
}

typedef struct{
  log_item items[10];
  char ** running_share_names;
  iron_process * running_share_processes;
  share_log_reader ** running_share_readers;
  log_item * running_share_last_item;
  size_t running_share_cnt;
}manager_ctx;

static void add_log(manager_ctx * ctx, char * msg, log_item_type type){
  log_item item;
  item.type = type;
  item.command = msg;
  push_log_item(ctx->items, array_count(ctx->items), item);
}

void add_running_share(manager_ctx * ctx, char * service, char * dir, char * name){

  if(name == NULL){
    for(int j = 0; j < 100; j++){
      name = fmtstr("share%i", j);
      bool ok = true;
      for(size_t i = 0; i < ctx->running_share_cnt; i++){
	if(strcmp(name, ctx->running_share_names[i]) == 0){
	  ok = false;
	  break;
	}
      }
      if(ok)
	break;
      //dealloc(service);
      dealloc(name);
      name = NULL;
    }
  }else{
    for(size_t i = 0; i < ctx->running_share_cnt; i++){
      if(strcmp(name, ctx->running_share_names[i]) == 0){
	add_log(ctx, fmtstr("name '%s' already exists", name), MANAGER_RESPONSE);
	return;
      }
    }
  }
  add_log(ctx, fmtstr("Adding share '%s' from '%s' dir:", name, service, dir), MANAGER_RESPONSE);
  iron_process new_proc;
  char data_log_name[strlen(name) + 10];
  sprintf(data_log_name, "%s.log", name);
  
  const char *args[] = {"share", "unused@0.0.0.0:test", dir, service, "--persist", "--data-log", data_log_name, NULL};
  int status = iron_process_run("./share", (const char **) args, &new_proc);
  if(status < 0){
    dealloc(service);
    add_log(ctx, fmtstr("Unable to start service"), MANAGER_RESPONSE);
    return;
  }
  add_log(ctx, fmtstr("started service.. "), MANAGER_RESPONSE);
  iron_sleep(0.2);
  for(int i = 0; i < 10; i++) printf("\n");
  iron_process_status proc_status = iron_process_get_status(new_proc);
  if(proc_status != IRON_PROCESS_RUNNING){
    dealloc(service);
    add_log(ctx, fmtstr("Unable to start service"), MANAGER_RESPONSE);
    return;
  }
  share_log_reader * reader = share_log_open_reader(data_log_name);
  dealloc(service);
  list_push(ctx->running_share_names, ctx->running_share_cnt, name);
  list_push(ctx->running_share_processes, ctx->running_share_cnt, new_proc);
  list_push(ctx->running_share_readers, ctx->running_share_cnt, reader);
  log_item item = {0};
  list_push(ctx->running_share_last_item, ctx->running_share_cnt, item);
  ctx->running_share_cnt += 1;

}

void remove_running_share(manager_ctx * ctx, char * name){
  bool found_item(char ** name2, manager_ctx * ctx2){
    UNUSED(ctx2);
    return strcmp(*name2, name) == 0;
  }
  char ** id = find1(ctx->running_share_names, ctx->running_share_cnt, found_item, ctx);
  if(id == NULL){
    add_log(ctx, fmtstr("Unable to find service '%s'", name), MANAGER_RESPONSE);
    return;
  }
  size_t offset = id - ctx->running_share_names;
  add_log(ctx, fmtstr("found share '%s' at %i", *id, offset), MANAGER_RESPONSE);
  dealloc(*id);
  iron_process_interupt(ctx->running_share_processes[offset]);
  list_remove2(ctx->running_share_names, ctx->running_share_cnt, offset);
  list_remove2(ctx->running_share_readers, ctx->running_share_cnt, offset);
  list_remove2(ctx->running_share_processes, ctx->running_share_cnt, offset);
  list_remove2(ctx->running_share_last_item, ctx->running_share_cnt, offset);
  ctx->running_share_cnt -= 1;
}

void handle_command(manager_ctx * ctx, char * command){
  UNUSED(ctx);
  UNUSED(command);
  log_item newitem;
  newitem.type = MANAGER_COMMAND;
  newitem.command = command;
  push_log_item(ctx->items, array_count(ctx->items), newitem);
  if(string_startswith(command, "add ")){
    char arg1[100], arg2[100], arg3[100];
    int result = sscanf(command, "add %s %s %s", arg1, arg2, arg3);
    if(result == 2 || result == 3){
      if(result == 2){
	logd("arg1: '%s', arg2: '%s'\n", arg1, arg2);
	sleep(2);
	add_running_share(ctx, fmtstr("%s", arg1), fmtstr("%s", arg2), NULL);
      }else{
	add_running_share(ctx,fmtstr("%s", arg1), fmtstr("%s", arg2), fmtstr("%s", arg3));
      }

    }else{
      add_log(ctx, fmtstr("unsupported number of args."),  MANAGER_RESPONSE);
      
    }
  }else if(string_startswith(command, "remove ")){
    char arg1[100];
    int r = sscanf(command, "remove %s", arg1);
    if(r != 1){
      add_log(ctx, fmtstr("Invalid number of arguments for remove"), MANAGER_RESPONSE);
    }else{
      remove_running_share(ctx, fmtstr("%s", arg1));
    }			   
  }else if(string_startswith(command, "help ")){

  }else if(string_startswith(command, "list")){
    for(size_t i = 0; i < ctx->running_share_cnt; i++){
      add_log(ctx, fmtstr("   %s", ctx->running_share_names[i]), MANAGER_RESPONSE);
    }
  }else{
    add_log(ctx, fmtstr("Unknown command '%s'", command), MANAGER_RESPONSE);
  }

      
}

int main(int argc, char ** argv){
  {
    UNUSED(argc);UNUSED(argv);
    static struct termios oldt, newt;
    
    /*tcgetattr gets the parameters of the current terminal
      STDIN_FILENO will tell tcgetattr that it should write the settings
      of stdin to oldt*/
    tcgetattr( STDIN_FILENO, &oldt);
    /*now the settings will be copied*/
    newt = oldt;
    
    /*ICANON normally takes care that one line at a time will be processed
      that means it will return if it sees a "\n" or an EOF or an EOL*/
    newt.c_lflag &= ~(ICANON | ECHO);          
    
    /*Those new settings will be set to STDIN
      TCSANOW tells tcsetattr to change attributes immediately. */
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);
  }
  
  char buff[100] = {0};
  char * buffer = buff;
  manager_ctx ctx = {0};

  pthread_t tid;
  pthread_t maintrd = pthread_self();
  
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
    for(size_t i = 0; i < ctx.running_share_cnt; i++){
      share_log_reader * reader = ctx.running_share_readers[i];
      while(0 != (read_items = share_log_reader_read(reader, items2, array_count(items2)))){
	for(int j = 0; j <read_items; j++){
	  // bug here: it might be needed to read further back beyoud i = 0;
	  ctx.running_share_last_item[i] = share_log_item_to_item(items2[j],
								   ctx.running_share_last_item[i]);
	  push_log_item(ctx.items, array_count(ctx.items), ctx.running_share_last_item[i]);
	}
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
