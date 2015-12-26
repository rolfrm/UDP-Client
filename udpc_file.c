// simple program to user UDPC to transfer a file between two peers
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <iron/log.h>
#include <iron/types.h>

#include "udpc.h"
#include "udpc_utils.h"
#include "service_descriptor.h"


#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include "udpc_send_file.h"

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

int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      udpc_file_serve(c2, NULL, (char *) ".");
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
