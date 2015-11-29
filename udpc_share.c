#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "udpc.h"
#include "udpc_utils.h"
#include "service_descriptor.h"

#include <iron/log.h>

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

int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);
  
  if(argc == 3){
    char * servicename = argv[1];
    char * dir = argv[2];
    struct stat dirst;
    stat(dir, &dirst);
    ASSERT(S_ISDIR(dirst.st_mode));
    dirscan scan_result = scan_directories(dir);
    udpc_service * con = udpc_login(argv[1]);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      size_t r = 0;
      char buf[1024];
      while(r == 0)
	r = udpc_read(c2, buf, sizeof(buf));
      void * rcv_str = buf;
      char * st = udpc_unpack_string(&rcv_str);
      if(strcmp(st, udpc_file_serve_service_name) == 0){
	udpc_file_serve(c2, buf);
      }else if(strcmp(st, udpc_speed_test_service_name) == 0){
	udpc_speed_serve(c2, buf);
      }else{
	loge("Unknown service '%s'\n", st);
      }
      
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc > 3){

    int delay = 40;
    int bufsize = 1500;
    char * in_file = argv[2];
    char * out_file = argv[3];
    if(argc > 4)
      sscanf(argv[4],"%i", &delay);
    if(argc > 5)
      sscanf(argv[5],"%i", &bufsize);
    logd("Delay: %i, buffer size: %i file: '%s' '%s'\n", delay, bufsize, in_file, out_file);
    udpc_connection * con = udpc_connect(argv[1]);
    if(con != NULL){
      udpc_file_client(con, delay, bufsize, in_file, out_file);
      udpc_close(con);
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
