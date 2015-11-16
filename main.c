#include <stdlib.h>
#include "udpc.h"
#include <iron/log.h>

typedef struct{
  char * username;
  char * service;
  char * host;
} service_item;
void print_service_item(service_item item);
service_item get_service_item(const char * service_string);
  
void _error(const char * file, int line, const char * msg, ...){
  loge("Got error at %s line %i\n", file,line);
  exit(255);
}

// UDPC sample program.
int main(){
  service_item it = get_service_item("rollo@asd.com:chat");
  print_service_item(it);logd("\n");
  return 0;
}
