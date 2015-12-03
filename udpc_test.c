#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h> //chdir
#include <time.h> //difftime

#include "udpc.h"
#include "udpc_utils.h"
#include <stdint.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/utils.h>
#include <iron/time.h>
#include "udpc_send_file.h"
#include "udpc_stream_check.h"
#include "udpc_dir_scan.h"

void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGSTOP);
  exit(255);
}

bool should_close = false;

void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}

void test_stuff(char ** argv){
  struct stat st;
  stat(argv[1], &st);
  
  if( S_ISREG(st.st_mode) ) {
    // file exists
    udpc_md5 md5 = udpc_file_md5(argv[1]);
    logd("MD5: ");
    udpc_print_md5(md5);
    logd("\n");
  } else if( S_ISDIR(st.st_mode)){
    // file doesn't exist
    dirscan dsc = scan_directories(argv[1]);
    logd("Found: %i files\n",dsc.cnt);
    dirscan_clean(&dsc);
  }
}

void ensure_directory(const char * path);

int main(int argc, char ** argv){
  srand(time(NULL));
  signal(SIGINT, handle_sigint);
  char buffer[10];
  if(argc == 2){
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);
    while(!should_close){
      logd("Retry\n");
      udpc_connection * c2 = udpc_listen(con);
      
      if(c2 == NULL)
	continue;
      udpc_set_timeout(c2, 100000);
      int i = -1;
      for(int _i = 0 ; _i < 10; _i++){
	if(i != -1)
	  i = udpc_read(c2, buffer, sizeof(buffer));
	
	udpc_write(c2, "ASD", sizeof("ASD"));
      }
      logd("OK!: %i\n", i);
      udpc_close(c2);
    }
    logd("Logging out..\n");
    udpc_logout(con);
  }else if(argc == 3){
    char * servicename = argv[1];
    udpc_connection * con = udpc_connect(servicename);
    udpc_set_timeout(con, 10000);
    int i = -1;
    for(int _i = 0; _i < 10; _i++){
      udpc_write(con, "asd", sizeof("asd"));
      iron_usleep(1000);
      if(i != -1)
	i = udpc_read(con, buffer, sizeof(buffer));
    }
    logd("OK! %i\n", i);
    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
