#include <sys/types.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <iron/mem.h>
#include <iron/log.h>
#include "udpc.h"
#include "service_descriptor.h"
#include "udp.h"
#include "ssl.h"
#include <pthread.h>
const int udpc_login_msg = 1;
const int udpc_response_ok_ip4 = 2;
const int udpc_mode_connect = 3;
const int udpc_response_ok = 4;
const int udpc_server_port = 11511;
struct _udpc_connection {
  ssl_client * cli;
  int fd;
};

void pack(const void * data, size_t data_len, void ** buffer, size_t * buffer_size){
  *buffer = ralloc(*buffer, *buffer_size + data_len);
  memcpy(*buffer + *buffer_size, data, data_len);
  *buffer_size += data_len;
}

void pack_int(int value, void ** buffer, size_t * buffer_size){
  pack(&value, sizeof(int), buffer, buffer_size);
}

int unpack_int(void ** buffer){
  int value = 0;
  memcpy(&value, *buffer, sizeof(int));
  *buffer = *buffer + sizeof(int);
  return value;
}

// connect to a server.
udpc_connection * udpc_login(const char * service){
  // Format of service must be name@host:service
  // 1. get server public key
  // 2. send service and public key encrypted
  // 3. do quick pubkey verification
  // 4. return connection struct.

  service_descriptor sitem = udpc_get_service_descriptor(service);
  ASSERT(sitem.host != NULL);
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("127.0.0.1", 0);
  int fd = udp_connect(&local_addr, &server_addr, false);
  ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &server_addr);
  
  { // send login request
    size_t s = strlen(service);
    void * buffer = NULL;
    size_t buffer_size = 0;
    pack_int(udpc_login_msg, &buffer, &buffer_size);
    pack(service, s, &buffer, &buffer_size);
    ssl_client_write(cli, buffer, buffer_size);
    free(buffer);
  }
  
  int response;
  size_t read_len = ssl_client_read(cli, &response, sizeof(response));
  ASSERT(read_len == sizeof(int));
  ASSERT(response == udpc_response_ok);
  udpc_connection * r = alloc0(sizeof(udpc_connection));
  r->cli = cli;
  r->fd = fd;
  return r;
}

udpc_connection * udpc_connect(const char * service){
  service_descriptor sitem = udpc_get_service_descriptor(service);
  ASSERT(sitem.host != NULL);
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("127.0.0.1", 0);
  int fd = udp_connect(&local_addr, &server_addr, false);
  ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &server_addr);
  {
    void * buffer = NULL;
    size_t buffer_size = 0;
    pack(&udpc_mode_connect, sizeof(udpc_mode_connect), &buffer, &buffer_size);
    pack(service, strlen(service), &buffer, &buffer_size);
    int zero = 0;
    pack(&zero, sizeof(zero), &buffer, &buffer_size);
    ssl_client_write(cli, buffer, buffer_size);
    free(buffer);
  }
  {
    char buffer[100];
    size_t read_size = ssl_client_read(cli, buffer, sizeof(buffer));
    ASSERT(read_size > 0);
    void * resp2 = buffer;
    // response should be [ip port remote_pubkey].
    int response_status = unpack_int(&resp2);
    ssl_client_close(cli);
    udp_close(fd);
    if(response_status == udpc_response_ok_ip4){
      int ip = unpack_int(&resp2);
      int port = unpack_int(&resp2);
      char addr[100];
      sprintf(addr, "%i.%i.%i.%i",ip & 0xFF, (ip << 8) & 0xFF, (ip << 16) & 0xFF, (ip << 24) & 0xFF);
      struct sockaddr_storage peer_addr = udp_get_addr(addr, port);;
      
      int _fd = udp_connect(&local_addr, &peer_addr, true);
      ssl_client * cli = ssl_start_client(_fd, (struct sockaddr *) &peer_addr);
      udpc_connection * connection = alloc0(sizeof(udpc_connection));
      connection->cli = cli;
      connection->fd = _fd;
      return connection;
    } 
  }
  
  return NULL;
}

void udpc_send(udpc_connection * client, void * buffer, size_t length){
  ssl_client_write(client->cli, buffer, length);
}

size_t udpc_receive(udpc_connection * client, void * buffer, size_t max_size){
  return ssl_client_read(client->cli, buffer, max_size);
}

typedef struct{
  struct sockaddr_storage client_addr;
  service_descriptor desc;
}service;

typedef struct{
  ssl_server_client * scli;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
  size_t service_cnt;
  service * services;
}connection_info;


static void * connection_handle(void * _info) {
  connection_info * info = _info;
  ssl_server_client * pinfo = info->scli;
  struct sockaddr_storage local_addr = info->local_addr;
  struct sockaddr_storage remote_addr = info->remote_addr;
  pthread_detach(pthread_self());
  int fd = udp_connect(&local_addr, &remote_addr, true);
  ssl_server_con * con = ssl_server_accept(pinfo, fd);
  int max_timeouts = 5;
  int num_timeouts = 0;
  while (num_timeouts < max_timeouts) {
    ssize_t len = 0;
    char buf[100];
    len = ssl_server_read(con, buf, sizeof(buf));
    if(len == 0){
      num_timeouts++;
      continue;
    }
    void * bufptr = buf;
    int status = unpack_int(&bufptr);
    if(status != udpc_login_msg) break;
    service srv;
    srv.desc = udpc_get_service_descriptor(bufptr);
    srv.client_addr = remote_addr;
    info->services = ralloc(info->services, sizeof(service) * (info->service_cnt + 1) );
    info->services[info->service_cnt] = srv;
    info->service_cnt += 1;
    ssl_server_write(con, &udpc_response_ok, sizeof(udpc_response_ok));
    break;
  }
  return NULL;
}

void udpc_start_server(char *local_address) {
  pthread_t tid;
  struct sockaddr_storage server_addr = udp_get_addr(local_address, udpc_server_port);
  int fd = udp_open(&server_addr);
  ssl_server * srv = ssl_setup_server(fd);
  while (1) {
    ssl_server_client * scli = ssl_server_listen(srv);
    if(scli == NULL)
      continue;
    connection_info * con_info = alloc0(sizeof(connection_info));
    con_info->scli = scli;
    con_info->local_addr = server_addr;
    con_info->remote_addr = ssl_server_client_addr(scli);
    if (pthread_create( &tid, NULL, connection_handle, con_info) != 0) {
      perror("pthread_create");
      exit(-1);
    }
  }
}
