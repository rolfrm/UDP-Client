
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

void udpc_con_send_slow(udpc_con * con, void * data, size_t len){
  uv_udp_send_t req;
  uv_buf_t buf = uv_buf_init(data, len);
  bool did_send = false;
  void send_cb(uv_udp_send_t * req, int status){
    did_send = true;
  }
  
  uv_udp_send(&req, con->conn, &buf, 1, con.host_addr, send_cb);
  while(false == did_send){}
}

static uv_buf_t alloc_cb(uv_handle_t* handle, size_t suggested_size) {
  static char slab[65536];
  return uv_buf_init(slab, sizeof(slab));
}

typedef struct{
  void * buffer;
  size_t size;
}udpc_rcv_buf;

void udpc_con_rcv(udpc_rcv_buf * buf, udpc_con * con){
  void rcv_cb(uv_udp_t * conn, ssize_t nread, uv_buf_t buf, struct sockaddr *addr, unsigned flags){
    if(nread == 0 && addr == NULL) return;
    buf->buffer = clone(buf.base, nread);
    buf->size = nread;
  }
  uv_udp_recv_start(con->conn, alloc_cb,  rcv_cv);
}

void get_server_pubkey(udpc_con * con, void * buffer){
  char * cmd = "PUBKEY";
  udpc_con_send_slow(con, cmd, sizeof(cmd));
}

typedef struct{
  bool ok;
  i64 challenge;
}udpc_login_response;

udpc_connection udpc_login(const char * service){
  // Format of service must be name@host:service
  // 1. get server public key
  // 2. send service and public key encrypted
  // 3. do quick pubkey verification
  // 4. return connection struct. 
  service_item sitem = get_service_item(service);
  ASSERT(sitem.host != NULL);
  rsa_pubkey my_pubkey;
  udpc_get_local_pubkey(&my_pubkey);
  rsa_pubkey serv_pubkey;
  udpc_get_server_pubkey(sitem.host, &serv_pubkey);
  
  size_t s = strlen(service);
  char * buffer = NULL;
  size_t buffer_size;
  pack(&s, sizeof(s), &buffer, &buffer_size);
  pack(service, s, &buffer, &buffer_size);
  pack(my_pubkey.buffer, my_pubkey.size, &buffer, &buffer_size);
  
  udpc_internal_con con = udpc_connect_encrypted(service.host, my_pubkey, serv_pubkey);
  udpc_internal_send(con, buffer, buffer_size);
  free(buffer);
  char * response = udpc_internal_rcv(con);
  udpc_login_response login_response = udpc_parse_login_response(response);
  free(response);
  ASSERT(login_response.ok);
  udpc_internal_send(con, &login_response.challenge, sizeof(login_response.challenge));
  char * challenge_response = udpc_internal_rcv(con);
  ASSERT(challenge_response[0] == 'O');
  free(challenge_response);
  return con;
}

udpc_connection udpc_connect(udpc_connection client, const char * service){
  service_item sitem = get_service_item(service);
  ASSERT(sitem.host != NULL);
  
}

void udpc_send(udpc_connection client, void * buffer, size_t length){

}

size_t udpc_receive(udpc_connection client, void * buffer, size_t max_size){

}

udpc_connection udpc_accept(udpc_connection client){

}
