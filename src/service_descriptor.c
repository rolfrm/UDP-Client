#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <iron/mem.h>
#include <iron/log.h>
#include "service_descriptor.h"

void udpc_print_service_descriptor(service_descriptor item){
  logd(" '%s' '%s' '%s' \n", item.username, item.service, item.host);
}

bool udpc_get_service_descriptor(const char * service_string, service_descriptor * out){
  char * at_index = strchr(service_string, '@');
  service_descriptor item = {0};
  if(at_index == NULL)
    return false;
  
  char * colon_index = strchr(at_index, ':');
  if(colon_index == NULL)
    return false;

  { // read user name.
    int username_length = at_index - service_string;
    char * username = alloc(username_length + 1);
    
    memcpy(username, service_string, username_length);
    username[username_length] = 0;
    
    item.username = username;
  }

  { // read host name
    at_index += 1;
    int hostname_length = colon_index - at_index;
    char * hostname =  alloc(hostname_length + 1);
    hostname[hostname_length] = 0;
    memcpy(hostname, at_index, hostname_length);

    item.host = hostname;
  }

  char * reststr = colon_index + 1;
  int restlen = strlen(reststr);
  item.service = iron_clone(reststr, restlen +1);
  *out = item;
  return true;
}

void udpc_delete_service_descriptor(service_descriptor item){
  dealloc(item.username);
  dealloc(item.service);
  dealloc(item.host);
}
