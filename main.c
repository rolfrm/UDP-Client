#include <stdlib.h>
#include "udpc.h"
#include "service_descriptor.h"
#include <iron/log.h>

typedef struct{
  char * username;
  char * service;
  char * host;
} service_item;

#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGINT);
  //exit(255);
  
}

// UDPC sample program.
int main(int argc, char ** argv){
  service_descriptor it = udpc_get_service_descriptor("rollo@127.0.0.1:chat");
  udpc_print_service_descriptor(it);logd("\n");

  if(argc == 2){
    udpc_connection * con = udpc_login(argv[1]);
    logd("Logged in..\n");
    udpc_connection * c2 = udpc_listen(con);
    logd("Got connection\n");
  }else if(argc > 2){
    udpc_connection * con = udpc_connect(argv[1]);
    logd("Connected to peer..\n");
  }else{
    udpc_start_server("127.0.0.1");
  }
    
  
  return 0;
}
