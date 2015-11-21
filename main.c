#include <stdlib.h>
#include "udpc.h"
#include "service_descriptor.h"
#include <iron/log.h>

typedef struct{
  char * username;
  char * service;
  char * host;
} service_item;
  
void _error(const char * file, int line, const char * msg, ...){
  loge("Got error at %s line %i\n", file,line);
  exit(255);
}

// UDPC sample program.
int main(int argc, char ** argv){
  service_descriptor it = udpc_get_service_descriptor("rollo@127.0.0.1:chat");
  udpc_print_service_descriptor(it);logd("\n");

  if(argc > 1){
    udpc_connection * con = udpc_login(argv[1]);
    logd("Logged in!\n");
  }else{
    udpc_start_server("127.0.0.1");
  }
    
  
  return 0;
}
