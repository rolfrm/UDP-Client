#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <iron/log.h>
#include <iron/mem.h>
#include "udp.h"
struct sockaddr_storage udp_get_addr(char * remote_address, int port){
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
  } remote_addr;
  ASSERT(strlen(remote_address) > 0);
  struct hostent * he = gethostbyname(remote_address);
  ASSERT(he != NULL);
  memset((void *) &remote_addr, 0, sizeof(struct sockaddr_storage));
  
  if(he->h_addrtype == AF_INET){
    int * iaddr = (int *) he->h_addr_list[0];
    unsigned char * addr = (unsigned char *) he->h_addr_list[0];
    //if(*iaddr == 0){
    //  addr[0] = 127; addr[1] = 0; addr[2] = 1; addr[3] = 1;
    //}else
    if(addr[0] == 127 && addr[1] == 0 && addr[2] == 0 && addr[3] == 1){
      *iaddr = 0;
    }else if(addr[0] == 127 && addr[1] == 0 && addr[2] == 1 && addr[3] == 1){
      *iaddr = 0;
    }
    logd("addr: %i.%i.%i.%i:%i\n", addr[0], addr[1], addr[2], addr[3], port);
    memcpy(&remote_addr.s4.sin_addr, he->h_addr_list[0], he->h_length);
    remote_addr.s4.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
    remote_addr.s4.sin_len = sizeof(struct sockaddr_in);
#endif
  }else if(he->h_addrtype == AF_INET6){
    memcpy(&remote_addr.s6.sin6_addr, he->h_addr_list[0], he->h_length);
    remote_addr.s6.sin6_family = AF_INET6;
#ifdef HAVE_SIN6_LEN
    remote_addr.s6.sin6_len = sizeof(struct sockaddr_in6);
#endif
  }else{
    ASSERT(false);
  }

  remote_addr.s4.sin_port = htons(port);
  return remote_addr.ss;
}

int udp_connect(struct sockaddr_storage * local, struct sockaddr_storage * remote, bool reuse){
  int fd = socket(local->ss_family, SOCK_DGRAM, 0);
  if (fd < 0) return fd;
  int on = 1;
  if(reuse){
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, (socklen_t) sizeof(on));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*) &on, (socklen_t) sizeof(on));
#endif
  }
  
  ASSERT(remote->ss_family == local->ss_family);

  if (local->ss_family == AF_INET) {
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in));
  } else {
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in6));
  }
  
  if (remote->ss_family == AF_INET) {
    connect(fd, (struct sockaddr *) remote, sizeof(struct sockaddr_in));
  } else {
    connect(fd, (struct sockaddr *) remote, sizeof(struct sockaddr_in6));
  }
  
  return fd;
}

int udp_get_port(int fd){
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
  } local_addr;
  socklen_t len2 = sizeof(local_addr);
  int ret = getsockname(fd, (struct sockaddr *) &local_addr, &len2);
  ASSERT(ret >= 0);
  return local_addr.s4.sin_port;
}

int udp_open(struct sockaddr_storage * local){
  const int on = 1, off = 0;
  int fd = socket(local->ss_family, SOCK_DGRAM, 0);
  if(fd < 0) return fd;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, (socklen_t) sizeof(on));
#ifdef SO_REUSEPORT
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*) &on, (socklen_t) sizeof(on));
#endif
  if (local->ss_family == AF_INET) {
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in));
  } else {
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off));
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in6));
  }
  return fd;
}

void udp_close(int fd){
  close(fd);
}
