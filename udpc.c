
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <uv.h>


#include <iron/mem.h>
#include <iron/log.h>
#include "udpc.h"

typedef struct _udpc_connection{
  uv_udp_t conn;
  uv_os_sock_t addr;
}udpc_con;

typedef struct{
  char * username;
  char * service;
  char * host;
} service_item;

void print_service_item(service_item item){
  logd(" '%s' '%s' '%s' \n", item.username, item.service, item.host);
}

service_item get_service_item(const char * service_string){
  char * at_index = strchr(service_string, '@');
  if(at_index == NULL)
    ERROR("invalid service string");
  char * colon_index = strchr(at_index, ':');
  if(colon_index == NULL)
    ERROR("Invalid service string");

  service_item item;
  item.username = strndup(service_string, (size_t)(at_index - service_string));
  at_index += 1;
  item.host = strndup(at_index, colon_index - at_index);
  item.service = strdup(colon_index + 1);

  return item;
}

void delete_service_item(service_item item){
  free(item.username);free(item.service);free(item.host);
}

udpc_connection udpc_login(const char * service){
  // format must be name@host:service
  udpc_con * con = alloc0(sizeof(udpc_con));
  uv_udp_init(uv_default_loop(), &con->conn);
  service_item sitem = get_service_item(service);
  uv_ip4_addr(sitem.host, 10011, &con->addr);
  uv_udp_send_t req;
  
  //  uv_udp_send(&req, &con->conn, 
  return con;
}

udpc_connection udpc_connect(udpc_connection client, const char * service){

}

void udpc_send(udpc_connection client, void * buffer, size_t length){

}

size_t udpc_receive(udpc_connection client, void * buffer, size_t max_size){

}
