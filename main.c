#include <stdlib.h>
#include "udpc.h"
#include "service_descriptor.h"
#include <iron/log.h>

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
    udpc_start_server(argv[1]);
  }else{
    udpc_start_server("0.0.0.0");
  }
  
  return 0;
}
