
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <uv.h>

#include <iron/mem.h>
#include <iron/log.h>
#include "udpc.h"
#include "service_descriptor.h"

static const int udpc_mode_login = 1;
static const int udpc_mode_connect = 2;
static const int udpc_mode_verify = 3;
static const int udpc_get_pubkey = 4;
static const int response_ok_ip4 = 1;
static const int response_no_service = 2;

typedef struct{
  rsa_pubkey local_pubkey;
  rsa_pubkey remote_pubkey;
  uv_udp_t conn;
  struct sockaddr_in remote_addr;
}udpc_internal_con;

typedef struct _udpc_connection{
  udpc_internal_con con;
}udpc_con;

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
  uv_udp_recv_start(con->conn, alloc_cb,  rcv_cb);
}

void udpc_con_end_rcv(udpc_con * con){
  uv_udpc_recv_stop(con->conn);
}

void get_server_pubkey(udpc_con * con, void * buffer){
  udpc_con_send_slow(con, &udpc_get_pubkey, sizeof(udpc_get_pubkey));
}

udpc_connection udpc_connect_encrypted(const char * host, int port, rsa_pubkey local_pubkey, rsa_pubkey remote_pubkey){
  struct sockaddr_in raddr = uv_ip4_addr(host, port);
  struct sockaddr_in laddr = uv_ip4_addr("127.0.0.1", 0);
  uv_udp_t conn;
  uv_udp_init(uv_default_loop(), &conn);
  uv_udp_bind(&conn, laddr, 0);
  udpc_con con;
  con.con.local_pubkey = local_pubkey;
  con.con.remote_pubkey = remote_pubkey;
  con.con.conn = conn;
  con.con.remote_addr = raddr;
  return clone(&con, sizeof(con));;
}

void udpc_internal_send(udpc_internal_con con, void * data, size_t len){

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
  service_descriptor sitem = get_service_descriptor(service);
  ASSERT(sitem.host != NULL);
  rsa_pubkey my_pubkey;
  udpc_get_local_pubkey(&my_pubkey);
  rsa_pubkey serv_pubkey;
  udpc_get_server_pubkey(sitem.host, &serv_pubkey);

  udpc_internal_con con = udpc_connect_encrypted(sitem.host, port, my_pubkey, serv_pubkey);

  {
    size_t s = strlen(service);
    char * buffer = NULL;
    size_t buffer_size;
    pack(&udpc_mode_login, sizeof(udpc_mode_login), &buffer, &buffer_size);
    pack(&s, sizeof(s), &buffer, &buffer_size);
    pack(service, s, &buffer, &buffer_size);
    pack(my_pubkey.buffer, my_pubkey.size, &buffer, &buffer_size);
    udpc_internal_send(con, buffer, buffer_size);  
    free(buffer);
  }
  
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
  service_descriptor sitem = get_service_descriptor(service);
  ASSERT(sitem.host != NULL);
  rsa_pubkey my_pubkey;
  udpc_get_local_pubkey(&my_pubkey);
  rsa_pubkey serv_pubkey;
  udpc_get_server_pubkey(sitem.host, &serv_pubkey);
  udpc_internal_con con = udpc_connect_encrypted(sitem.host, my_pubkey, serv_pubkey);
  {
    char * buffer = NULL;
    size_t buffer_size = 0;
    pack(&udpc_mode_connect, sizeof(udpc_mode_connect), &buffer, &buffer_size);
    pack(service, strlen(service), &buffer, &buffer_size);
    pack(my_pubkey.buffer, my_pubkey.size, &buffer, &buffer_size);
    udpc_internal_send(con, buffer, buffer_size);
    free(buffer);
  }
  void * rsp = udpc_internal_rcv(con);
  int response_status = 0;
  memcpy(&response_status, rsp, sizeof(response_status));
  if(response_status == response_ok_ip4){
    void * rsp2 = rsp + sizeof(response_status);
    int ip;
    int port;
    memcpy(&ip, rsp2, sizeof(int));
    rsp2 += sizeof(int);
    memcpy(&ip, rsp2, sizeof(int));
    rsp2 += sizeof(int);
    memcpy(&port, rsp2, sizeof(int));
    rsa_pubkey other_pubkey;    
    rsp2 = udpc_read_pubkey(&other_pubkey, rsp2);
    
    char addr_buf[30];
    sprintf(buffer, "%i.%i.%i.%i",ip & 255, (ip << 8) & 255,
	    (ip << 16) & 255, (ip << 24) & 255);
    udpc_internal_con con = udpc_connect_encrypted(addr_buf, my_pubkey, other_pubkey, port);
  }else{
    ASSERT(false);
  }

    
   
}

void udpc_send(udpc_connection client, void * buffer, size_t length){

}

size_t udpc_receive(udpc_connection client, void * buffer, size_t max_size){

}

udpc_connection udpc_accept(udpc_connection client){

}
