#include <stdarg.h>
#include <string.h>
#include <iron/mem.h>
#include <iron/log.h>
#include "service_descriptor.h"

void udpc_print_service_descriptor(service_descriptor item){
  logd(" '%s' '%s' '%s' \n", item.username, item.service, item.host);
}

service_descriptor udpc_get_service_descriptor(const char * service_string){
  char * at_index = strchr(service_string, '@');
  service_descriptor item = {0};
  if(at_index == NULL){
    ERROR("invalid service string");
    return item;
  }

  char * colon_index = strchr(at_index, ':');
  if(colon_index == NULL){
    ERROR("Invalid service string");
    return item;
  }

  item.username = strndup(service_string, (size_t)(at_index - service_string));
  at_index += 1;
  item.host = strndup(at_index, colon_index - at_index);
  item.service = strdup(colon_index + 1);

  return item;
}

void udpc_delete_service_descriptor(service_descriptor item){
  dealloc(item.username);dealloc(item.service);dealloc(item.host);
}

