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

void ensure_directory(const char * path);

int main(int argc, char ** argv){
  srand(time(NULL));
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);
    while(!should_close){
      logd("Retry\n");
      udpc_connection * c2 = udpc_listen(con);
      if(c2 == NULL)
	continue;
      //char buf[1024];
      //int r = udpc_read(c2, buf, sizeof(buf));
      //if(r == -1)
      //break;

      udpc_set_timeout(c2, 100000);
      dirscan scan_result = scan_directories("testdir");
      udpc_dirscan_serve(c2, scan_result, 1000, 1400, NULL);
      dirscan_clean(&scan_result);
      udpc_close(c2);
    }
    logd("Logging out..\n");
    udpc_logout(con);
  }else if(argc == 3){
    char * servicename = argv[1];
  getcon:;
    udpc_connection * con = udpc_connect(servicename);
    if(con == NULL)
      goto getcon;
    udpc_set_timeout(con, 10000);
  do_dirscan:;
    dirscan ext_dir;
    int ok = udpc_dirscan_client(con, &ext_dir);
    if(!ok) goto do_dirscan;
    logd("Got dir!\n");
    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
