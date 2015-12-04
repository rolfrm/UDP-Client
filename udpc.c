#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <iron/mem.h>
#include <iron/log.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "service_descriptor.h"
#include "udp.h"
#include "ssl.h"
#include <pthread.h>
const int udpc_login_msg = 1;
const int udpc_response_ok_ip4 = 2;
const int udpc_mode_connect = 3;
const int udpc_response_ok = 4;
const int udpc_response_error = 6;
const int udpc_mode_disconnect = 5;

const int udpc_server_port = 11511;

struct _udpc_connection {
  ssl_client * cli;
  int fd;
  ssl_server * srv;
  ssl_server_client * scli;
  ssl_server_con * con;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
};

struct _udpc_service{
  ssl_server * srv;
  int fd;
  service_descriptor service;
  struct sockaddr_storage local_addr;
};

char ** udpc_errors = NULL;
size_t udpc_error_cnt = 0;

void udpc_push_error(const char * error_message){
  udpc_errors = realloc(udpc_errors, sizeof(udpc_errors[0]) * (udpc_error_cnt + 1));
  udpc_errors[udpc_error_cnt] = iron_clone(error_message, strlen(error_message) + 1);
  udpc_error_cnt += 1;
}

char * udpc_pop_error(){
  if(udpc_error_cnt == 0)
    return NULL;
  char * err = udpc_errors[0];
  memmove(udpc_errors, udpc_errors + 1, (udpc_error_cnt - 1) * sizeof(udpc_errors[0]));
  return err;
}

// connect to a server. Format of service must be name@host:service
udpc_service * udpc_login(const char * service){
  service_descriptor sitem = udpc_get_service_descriptor(service);
  if(sitem.host == NULL)
     return NULL;
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("0.0.0.0", 0);
  
  int fd2 = udp_connect(&local_addr, &server_addr, false);
  int fd = udp_open(&local_addr);
  int port = udp_get_port(fd);
  ssl_client * cli = ssl_start_client(fd2, (struct sockaddr *) &server_addr);
  ssl_set_timeout(cli, 1000000);
 retry:
  { // send login request
    size_t s = strlen(service);
    void * buffer = NULL;
    size_t buffer_size = 0;
    udpc_pack_int(udpc_login_msg, &buffer, &buffer_size);
    udpc_pack_int(port, &buffer, &buffer_size);
    udpc_pack(service, s, &buffer, &buffer_size);

    ssl_client_write(cli, buffer, buffer_size);
    free(buffer);
  }
  
  int response;
  int read_len = ssl_client_read(cli, &response, sizeof(response));
  if(read_len == -1)
    goto retry;
  ASSERT(read_len == sizeof(int));
  ASSERT(response == udpc_response_ok);
  ssl_client_close(cli);
  ssl_server * srv = ssl_setup_server(fd);
  udpc_service * r = alloc0(sizeof(udpc_service));
  ((struct sockaddr_in *) &local_addr)->sin_port = port;
  r->fd = fd2;
  r->srv = srv;
  r->local_addr = local_addr;
  r->service = sitem;
  return r;
}

void udpc_logout(udpc_service * con){
  
  if(con->srv != NULL){
    struct sockaddr_storage udpc_server_addr = udp_get_addr(con->service.host, udpc_server_port);
    struct sockaddr_storage local_addr = udp_get_addr("0.0.0.0", 0);
    int fd = udp_connect(&local_addr, &udpc_server_addr, false);
    ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &udpc_server_addr);
    { // send logout request.
      void * buffer = NULL;
      size_t buffer_size = 0;
      udpc_pack_int(udpc_mode_disconnect, &buffer, &buffer_size);
      udpc_pack_string(con->service.username, &buffer, &buffer_size);
      udpc_pack_string(con->service.service, &buffer, &buffer_size);
    retry:
      ssl_client_write(cli, buffer, buffer_size);
      int read = ssl_client_read(cli, buffer, buffer_size);
      if(read == -1){
	goto retry;
      }
      void * bufptr = buffer;
      int status = udpc_unpack_int(&bufptr);
      ASSERT(status == udpc_response_ok);
      free(buffer);
    }
    ssl_client_close(cli);
    udp_close(fd);
    ssl_server_cleanup(con->srv);
  }
  udp_close(con->fd);
  free(con);
}

udpc_connection * udpc_listen(udpc_service * con){
  ssl_server_client * scli = ssl_server_listen(con->srv);
  if(scli == NULL) return NULL;
  struct sockaddr_storage cli_ss = ssl_server_client_addr(scli);
  int fd = udp_connect(&con->local_addr, &cli_ss, true);
  ASSERT(fd > 0);
  ssl_server_con * _con = ssl_server_accept(scli, fd);
  udpc_connection * c = alloc0(sizeof(udpc_connection));
  c->con = _con;
  return c;
}

udpc_connection * udpc_connect(const char * service){
  service_descriptor sitem = udpc_get_service_descriptor(service);
  if(sitem.host == NULL)
    return NULL;
  
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("0.0.0.0", 0);
 retry2:;
  int fd = udp_connect(&local_addr, &server_addr, false);

  ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &server_addr);

  ssl_set_timeout(cli, 1000000);
  
  { // send [CONNECT *service* 0]
    void * buffer = NULL;
    size_t buffer_size = 0;
    udpc_pack_int(udpc_mode_connect, &buffer, &buffer_size);
    udpc_pack_string(service, &buffer, &buffer_size);
    ssl_client_write(cli, buffer, buffer_size);
    free(buffer);
  }
  
  {// receive [port peer_addr] and connect client
    char buffer[100];
    int read_size = ssl_client_read(cli, buffer, sizeof(buffer));
    logd("Read size: %i\n", read_size);
    if(read_size == -1){
      ssl_client_close(cli);
      udp_close(fd);
      goto retry2;
    }
    ASSERT(read_size > 0);
    void * resp2 = buffer;

    int response_status = udpc_unpack_int(&resp2);
    ssl_client_close(cli);
    udp_close(fd);
    if(response_status == udpc_response_ok_ip4){
      struct sockaddr_storage peer_addr;
      int port = udpc_unpack_int(&resp2);
      udpc_unpack(&peer_addr, sizeof(peer_addr), &resp2);
      ((struct sockaddr_in *)&peer_addr)->sin_port = port;
      int _fd = udp_connect(&local_addr, &peer_addr, false);
      ssl_client * cli = ssl_start_client(_fd, (struct sockaddr *)&peer_addr);
      udpc_connection * connection = alloc0(sizeof(udpc_connection));
      connection->cli = cli;
      connection->fd = _fd;
      connection->local_addr = local_addr;
      connection->remote_addr = peer_addr;
      return connection;
    } 
  }
  
  return NULL;
}

void udpc_set_timeout(udpc_connection * client, int us){
  if(client->cli != NULL){
    ssl_set_timeout(client->cli, us);
  }else if(client->con != NULL){
    ssl_server_set_timeout(client->con, us);
  }
}

void udpc_write(udpc_connection * client, const void * buffer, size_t length){
  if(client->cli != NULL)
    ssl_client_write(client->cli, buffer, length);
  else if(client->con != NULL)
    ssl_server_write(client->con, buffer, length);
}

int udpc_read(udpc_connection * client, void * buffer, size_t max_size){
  if(client->cli != NULL)
    return ssl_client_read(client->cli, buffer, max_size);
  else if(client->con != NULL)
    return ssl_server_read(client->con, buffer, max_size);
  return -1;
}

void udpc_close(udpc_connection * con){
  if(con->cli != NULL){
    ssl_client_close(con->cli);
    udp_close(con->fd);
  }
  if(con->scli != NULL)
    ssl_server_close(con->scli);
  free(con);
}

typedef struct{
  struct sockaddr_storage client_addr;
  service_descriptor desc;
  int port;
}service;

typedef struct{
  size_t service_cnt;
  service * services;
}service_server;

typedef struct{
  ssl_server_client * scli;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
  service_server * server;
}connection_info;


static void * connection_handle(void * _info) {
  connection_info * info = _info;
  ssl_server_client * pinfo = info->scli;
  struct sockaddr_storage local_addr = info->local_addr;
  struct sockaddr_storage remote_addr = info->remote_addr;
  pthread_detach(pthread_self());
  int fd = udp_connect(&local_addr, &remote_addr, true);
  ssl_server_con * con = ssl_server_accept(pinfo, fd);
  
  char buf[100];
  int len = ssl_server_read(con, buf, sizeof(buf));
  if(len == -1){
    ssl_server_close(pinfo);
    return NULL;
  }
  void * bufptr = buf;
  int status = udpc_unpack_int(&bufptr);
  if(status == udpc_login_msg){
    logd("UDPC LOGIN\n");
    int port = udpc_unpack_int(&bufptr);
    service srv;
    srv.desc = udpc_get_service_descriptor(bufptr);
    srv.client_addr = remote_addr;
    srv.port = port;
    info->server->services = ralloc(info->server->services, sizeof(service) * (info->server->service_cnt + 1) );
    info->server->services[info->server->service_cnt] = srv;
    info->server->service_cnt += 1;
    ssl_server_write(con, &udpc_response_ok, sizeof(udpc_response_ok));
  }else if( status == udpc_mode_connect){
    logd("UDPC CONNECT\n");
    service_descriptor d = udpc_get_service_descriptor(bufptr);
    
    int cnt = info->server->service_cnt;
    for(int i = 0; i < cnt; i++){
      service s1 = info->server->services[i];
      if(strcmp(s1.desc.username, d.username) == 0 &&
	 strcmp(s1.desc.service, d.service) == 0){	\
	
	void * buffer = NULL;
	size_t buf_size = 0;
	udpc_pack_int(udpc_response_ok_ip4, &buffer, &buf_size);
	udpc_pack_int(s1.port, &buffer, &buf_size);
	udpc_pack(&s1.client_addr, sizeof(s1.client_addr), &buffer, &buf_size);
	ssl_server_write(con, buffer, buf_size);
	ssl_server_close(pinfo);
	return NULL;
      }
    }
    ssl_server_write(con, &udpc_response_error, sizeof(udpc_response_error));

  }else if(status == udpc_mode_disconnect){
    logd("UDPC DISCONNECT\n");
    char * username = bufptr;
    bufptr += strlen(bufptr) + 1;
    char * servicename = bufptr;
    int cnt = info->server->service_cnt;
    for(int i = 0; i < cnt; i++){
      service s1 = info->server->services[i];
      if(strcmp(s1.desc.username, username) == 0 &&
	 strcmp(s1.desc.service, servicename) == 0){
	for(int j = i + 1; j < cnt; j++){
	  info->server->services[j - 1] = info->server->services[j];
	}
	info->server->service_cnt -=1;
	logd("OK\n");
	ssl_server_write(con, &udpc_response_ok, sizeof(udpc_response_ok));
	ssl_server_close(pinfo);
	return NULL;
      }
    }
    ssl_server_write(con, &udpc_response_error, sizeof(udpc_response_error));
    ssl_server_close(pinfo);
  }
  ssl_server_close(pinfo);
  return NULL;
}

void udpc_start_server(const char *local_address) {
  
  pthread_t tid;
  service_server server;
  server.service_cnt = 0;
  server.services = NULL;
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
    con_info->server = &server;
    if (pthread_create( &tid, NULL, connection_handle, con_info) != 0) {
      loge("pthread_create");
      exit(-1);
    }
  }
}
