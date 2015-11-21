#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <iron/log.h>
#include <iron/mem.h>
#include "ssl.h"
#include "udp.h"

typedef struct{
  ssl_server_client * scli;
  struct sockaddr_storage local_addr;
  struct sockaddr_storage remote_addr;
}connection_info;
static int conid = 0;

void* connection_handle(void *info) {
    char buf[100];
    ssl_server_client * pinfo = ((connection_info *) info)->scli;
    struct sockaddr_storage local_addr = ((connection_info *) info)->local_addr;
    struct sockaddr_storage remote_addr = ((connection_info *) info)->remote_addr;
    int conidx = conid++;
    pthread_detach(pthread_self());
    int fd = udp_connect(&local_addr, &remote_addr, true);
    ssl_server_con * con = ssl_server_accept(pinfo, fd);
    int max_timeouts = 5;
    int num_timeouts = 0;
    while (num_timeouts < max_timeouts) {

      int reading = 1;
      ssize_t len = 0;
      while (reading) {

	len = ssl_server_read(con, buf, sizeof(buf));
	printf("Thread %i: received '%s' \n", conidx, buf);
	if(len > 0)
	  break;
      }

      if (len > 0) {
	char * sendbuf = "Hello sir!\n";
	ssl_server_write(con, sendbuf, strlen(sendbuf) + 1);
      }
    }
    ssl_server_close(pinfo);


  cleanup:
    close(fd);
    free(info);
    //SSL_free(ssl);
    //ERR_remove_state(0);
    printf("Thread %i: done, connection closed.\n", 0);//id_function());
    pthread_exit( (void *) NULL );
  }

  void start_server(int port, char *local_address) {

    pthread_t tid;
    struct sockaddr_storage server_addr = udp_get_addr(local_address, port);
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

  void start_client(char *remote_address, char *local_address, int port, int length, int messagenumber) {
    struct sockaddr_storage remote = udp_get_addr(remote_address, port)
      , local = udp_get_addr(local_address, 0);
    
    int fd = udp_connect(&local, &remote, false);
    ssl_client * cli = ssl_start_client(fd, (struct sockaddr *) &remote);

    while (messagenumber < 0) {
      char * buf = "Hello?";      
      if (messagenumber > 0) { 
	ssl_client_write(cli, buf, strlen(buf) + 1);
	
	if (messagenumber == 2)
	  ssl_client_heartbeat(cli);

	/* Shut down if all messages sent */
	if (messagenumber == 0)
	  ssl_client_close(cli);
      }

      int reading = 1;
      while (reading) {
	char readbuf[100];
	int len = ssl_client_read(cli, readbuf, sizeof(readbuf));
	if(len > 0){
	  reading = 0;
	  printf("Read: %s\n", readbuf);
	  messagenumber--;
	}
      }
    }

    close(fd);
    printf("Connection closed.\n");

  }

  int main(int argc, char **argv)
  {
    int port = 23333;
    int length = 200;
    int messagenumber = 5;
    if (argc > 1)
      start_client(argv[1], "127.0.0.1", port, length, messagenumber);
    else
      start_server(port, "127.0.0.1");

    return 0;

  cmd_err:
    return 1;
  }

#include <signal.h>

  void _error(const char * file, int line, const char * msg, ...){
    loge("Got error at %s line %i\n", file,line);
    raise(SIGINT);
    exit(255);
  }
