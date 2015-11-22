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

  if(argc == 2){
    udpc_connection * con = udpc_login(argv[1]);
    logd("Logged in..\n");
    udpc_connection * c2 = udpc_listen(con);
    logd("Got connection\n");
    char buffer[10];
    size_t r = 0;
    while(r == 0)
      r = udpc_receive(c2,buffer, sizeof(buffer));
    logd("Received: '%s'\n", buffer);
    udpc_send(c2, "World", sizeof("World"));
    udpc_close(c2);
    udpc_close(con);
    
    
  }else if(argc > 2){
    udpc_connection * con = udpc_connect(argv[1]);
    logd("Connected to peer..\n");
    udpc_send(con, "Hello", sizeof("Hello"));
    size_t r = 0;
    char buffer[10];
    while(r == 0)
      r = udpc_receive(con, buffer, sizeof(buffer));
    logd("Received: '%s'\n", buffer);
    udpc_close(con);
  }else{
    udpc_start_server("127.0.0.1");
  }
    
  
  return 0;
}
