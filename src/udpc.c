#include <arpa/inet.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <iron/mem.h>
#include <iron/log.h>
#include <iron/types.h>
#include <iron/process.h>
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

int udpc_server_port = 11511;

struct _udpc_connection {
  bool is_server;
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
  
  int fd2;
  service_descriptor service;
  struct sockaddr_storage local_addr;
};

// connect to a server. Format of service must be name@host:service
udpc_service * udpc_login(const char * service){
  service_descriptor sitem;
  if(false == udpc_get_service_descriptor(service, &sitem))
    return NULL;
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("0.0.0.0", 0);

  int fd2 = udp_connect(&local_addr, &server_addr, false);
  int fd = udp_open(&local_addr);
  if(fd == -1 || fd2 == -1)
    ERROR("Unable to open socket");
  
  int port = udp_get_port(fd);
  ssl_client * cli = ssl_start_client(fd2, (struct sockaddr *) &server_addr);
  if(cli == NULL){
    logd("ssl_start_client error\n");
    udp_close(fd);
    udp_close(fd2);
    return NULL;
  }
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
  if(read_len < 0)
    goto retry;
  ASSERT(read_len == sizeof(int));
  ASSERT(response == udpc_response_ok);
  ssl_client_close(cli);
  ssl_server * srv = ssl_setup_server(fd);
  if(srv == NULL) return NULL;
  
  udpc_service * r = alloc(sizeof(udpc_service));
  ((struct sockaddr_in *) &local_addr)->sin_port = port;
  r->fd2 = fd2;
  r->fd = fd;
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
    if(cli == NULL){
      udp_close(fd);
      udpc_logout(con);

      return;
    }
    
    { // send logout request.
      void * buffer = NULL;
      size_t buffer_size = 0;
      udpc_pack_int(udpc_mode_disconnect, &buffer, &buffer_size);
      udpc_pack_string(con->service.username, &buffer, &buffer_size);
      udpc_pack_string(con->service.service, &buffer, &buffer_size);
    retry:
      ssl_client_write(cli, buffer, buffer_size);
      int read = ssl_client_read(cli, buffer, buffer_size);
      if(read < 0){
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
  udp_close(con->fd2);
  free(con);
}

udpc_connection * udpc_listen(udpc_service * con){
  ssl_server_client * scli = ssl_server_listen(con->srv);
  if(scli == NULL) return NULL;
  struct sockaddr_storage cli_ss = ssl_server_client_addr(scli);
  int fd = udp_connect(&con->local_addr, &cli_ss, true);
  ASSERT(fd > 0);
  if(fd <= 0)
    ERROR("Invalid socket created");
  ssl_server_con * _con = ssl_server_accept(scli, fd);
  if(_con == NULL){
    ERROR("Unable to create ssl server connection");
    udp_close(fd);
    ssl_server_close(scli);
    free(scli);
    return NULL;
  }
  udpc_connection * c = alloc(sizeof(udpc_connection));
  c->is_server = true;
  c->con = _con;
  c->fd = fd;
  return c;
}
void iron_sleep(double);

iron_mutex connect_mutex;
bool connect_mutex_initialized = false;

udpc_connection * udpc_connect(const char * service){
  if(!connect_mutex_initialized){
    connect_mutex = iron_mutex_create();
  }
  iron_mutex_lock(connect_mutex);
  
  ASSERT(service != NULL);
  service_descriptor sitem;
  if(false == udpc_get_service_descriptor(service, &sitem)){
    logd("no service detected: '%s'\n", service);
    iron_mutex_unlock(connect_mutex);
    return NULL;
  }
  
  struct sockaddr_storage server_addr = udp_get_addr(sitem.host, udpc_server_port);
  struct sockaddr_storage local_addr = udp_get_addr("0.0.0.0", 0);
 retry2:;
  int fd = udp_connect(&local_addr, &server_addr, false);
  if(fd == -1){
    iron_sleep(0.1);
    // wrecklessly try to reconnect
    // might not be necessesart.
    iron_mutex_unlock(connect_mutex);
    return udpc_connect(service);
    
  }
  ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &server_addr);
  if(cli == NULL){
    iron_sleep(0.1);
    udp_close(fd);
    goto retry2;
  }
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
    if(read_size < 0){
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
      if(cli == NULL){
	iron_mutex_unlock(connect_mutex);
	return NULL;
      }
      udpc_connection * connection = alloc(sizeof(udpc_connection));
      connection->cli = cli;
      connection->fd = _fd;
      connection->local_addr = local_addr;
      connection->remote_addr = peer_addr;
      connection->is_server = false;
      iron_mutex_unlock(connect_mutex);
      return connection;
    } 
  }
  
  return NULL;
}

void udpc_set_timeout(udpc_connection * client, int us){
  if(client->is_server)
    ssl_server_set_timeout(client->con, us);
  else
    ssl_set_timeout(client->cli, us);
}

int udpc_get_timeout(udpc_connection * client){
  if(client->is_server)
    return ssl_server_get_timeout(client->con);
  return ssl_get_timeout(client->cli);
}

void udpc_write(udpc_connection * client, const void * buffer, size_t length){
  if(client->is_server)
    ssl_server_write(client->con, buffer, length);
  else
    ssl_client_write(client->cli, buffer, length);
}

int udpc_read(udpc_connection * client, void * buffer, size_t max_size){
  static size_t trashbuffer;
  ASSERT(buffer != NULL || max_size == 0);
  if(buffer == NULL){
    buffer = &trashbuffer;
    max_size = sizeof(trashbuffer);
  }
  
  if(client->is_server)
    return ssl_server_read(client->con, buffer, max_size);
  return ssl_client_read(client->cli, buffer, max_size);
}

int udpc_peek(udpc_connection * client, void * buffer, size_t max_size){
  
  if(client->is_server)
    return ssl_server_peek(client->con, buffer, max_size);
  return ssl_client_peek(client->cli, buffer, max_size);
}

int udpc_pending(udpc_connection * client){
  if(client->is_server)
    return ssl_server_pending(client->con);
  return ssl_client_pending(client->cli);
}

void udpc_close(udpc_connection * con){
  if(con->is_server)
    ssl_server_close((void *)con->con);
  else
    ssl_client_close(con->cli);
  udp_close(con->fd); 
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
  iron_mutex connect_mutex;
}service_server;

typedef struct{
  ssl_server_client * scli;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
  service_server * server;
}connection_info;

struct sockaddr_storage get_loopback(int port){
  return udp_get_addr("0.0.0.0", port);
}

bool sockaddr_is_localhost(struct sockaddr_storage addr){
  unsigned char * ip = (unsigned char *)& (((struct sockaddr_in *)&addr)->sin_addr.s_addr);
  if(ip[0] == 127 && ip[1] == 0 && ip[2] == 0 && ip[3] == 1)
    return true;
  return false;
}

bool sockaddr_is_server(struct sockaddr_storage addr){
  unsigned char * ip = (unsigned char *)& (((struct sockaddr_in *)&addr)->sin_addr.s_addr);
  if(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0)
    return true;
  return false;
}


static void * connection_handle(void * _info) {
  connection_info * info = _info;
  ssl_server_client * pinfo = info->scli;
  struct sockaddr_storage local_addr = info->local_addr;
  struct sockaddr_storage remote_addr = info->remote_addr;
  pthread_detach(pthread_self());
  iron_mutex_lock(info->server->connect_mutex);
  int fd = udp_connect(&local_addr, &remote_addr, true);
 retry_con:;
  ssl_server_con * con = ssl_server_accept(pinfo, fd);
  if(con == NULL)
    goto retry_con;
  
  
  char buf[100];
  int len = ssl_server_read(con, buf, sizeof(buf));
  if(len == -1){
    udp_close(fd);
    ssl_server_close(pinfo);
    iron_mutex_unlock(info->server->connect_mutex);
    return NULL;
  }
  buf[len] = 0;
  void * bufptr = buf;
  int status = udpc_unpack_int(&bufptr);
  if(status == udpc_login_msg){
    logd("UDPC LOGIN\n");
    int port = udpc_unpack_int(&bufptr);
    service srv;
    bool ok = udpc_get_service_descriptor(bufptr, &srv.desc);
    if(!ok)
      goto error;
     
    if(sockaddr_is_localhost(remote_addr))
      srv.client_addr = get_loopback(udpc_server_port);
    else
      srv.client_addr = remote_addr;

    srv.port = port;
    service_descriptor d = srv.desc;
    int cnt = info->server->service_cnt;
    for(int i = 0; i < cnt; i++){
      service s1 = info->server->services[i];
      if(strcmp(s1.desc.username, d.username) == 0 &&
	 strcmp(s1.desc.service, d.service) == 0){
	logd("UDPC RECONNECT\n");
	info->server->services[i] = srv;
	ssl_server_write(con, &udpc_response_ok, sizeof(udpc_response_ok));
	ssl_server_close(pinfo);
	udp_close(fd);
	iron_mutex_unlock(info->server->connect_mutex);
	return NULL;
      }
    }

    info->server->services = ralloc(info->server->services, sizeof(service) * (info->server->service_cnt + 1) );
    info->server->services[info->server->service_cnt] = srv;
    info->server->service_cnt += 1;
    ssl_server_write(con, &udpc_response_ok, sizeof(udpc_response_ok));
  }else if( status == udpc_mode_connect){
    logd("UDPC CONNECT\n");
    
    service_descriptor d;
    if(false == udpc_get_service_descriptor(bufptr, &d)){
      goto error;
    }
    
    int cnt = info->server->service_cnt;
    for(int i = 0; i < cnt; i++){
      service s1 = info->server->services[i];
      if(strcmp(s1.desc.username, d.username) == 0 &&
	 strcmp(s1.desc.service, d.service) == 0){
	void * buffer = NULL;
	size_t buf_size = 0;
	udpc_pack_int(udpc_response_ok_ip4, &buffer, &buf_size);
	udpc_pack_int(s1.port, &buffer, &buf_size);
	udpc_pack(&s1.client_addr, sizeof(s1.client_addr), &buffer, &buf_size);
	ssl_server_write(con, buffer, buf_size);
	ssl_server_close(pinfo);
	udp_close(fd);
	iron_mutex_unlock(info->server->connect_mutex);	
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
	udp_close(fd);
	iron_mutex_unlock(info->server->connect_mutex);
	return NULL;
      }
    }
    goto error;
  }
  udp_close(fd);
  ssl_server_close(pinfo);
  iron_mutex_unlock(info->server->connect_mutex);
  return NULL;
  
 error:
  ssl_server_write(con, &udpc_response_error, sizeof(udpc_response_error));
  ssl_server_close(pinfo);
  iron_mutex_unlock(info->server->connect_mutex);
  return NULL;  
}

void udpc_start_server(const char *local_address) {
  service_server server;
  server.service_cnt = 0;
  server.services = NULL;
  server.connect_mutex = iron_mutex_create();
  struct sockaddr_storage server_addr = udp_get_addr(local_address, udpc_server_port);
  int fd = udp_open(&server_addr);
  ssl_server * srv = ssl_setup_server(fd);
  while (1) {
    ssl_server_client * scli = ssl_server_listen (srv);
    if(scli == NULL)
      continue;
    connection_info * con_info = alloc(sizeof(connection_info));
    con_info->scli = scli;
    con_info->local_addr = server_addr;
    con_info->remote_addr = ssl_server_client_addr(scli);
    con_info->server = &server;
    connection_handle(con_info);
    //if (false && pthread_create( &tid, NULL, connection_handle, con_info) != 0) 
    //  ERROR("pthread create failed.");
  }
}
