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

  item.username = strndup(service_string, (size_t)(at_index - service_string));
  at_index += 1;
  item.host = strndup(at_index, colon_index - at_index);
  item.service = strdup(colon_index + 1);
  *out = item;
  return true;
}

void udpc_delete_service_descriptor(service_descriptor item){
  dealloc(item.username);
  dealloc(item.service);
  dealloc(item.host);
}
