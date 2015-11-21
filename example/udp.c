#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
  memset((void *) &remote_addr, 0, sizeof(struct sockaddr_storage));
  if (inet_pton(AF_INET, remote_address, &remote_addr.s4.sin_addr) == 1) {
    remote_addr.s4.sin_family = AF_INET;
#ifdef HAVE_SIN_LEN
    remote_addr.s4.sin_len = sizeof(struct sockaddr_in);
#endif
    remote_addr.s4.sin_port = htons(port);
  } else if (inet_pton(AF_INET6, remote_address, &remote_addr.s6.sin6_addr) == 1) {
    remote_addr.s6.sin6_family = AF_INET6;
#ifdef HAVE_SIN6_LEN
    remote_addr.s6.sin6_len = sizeof(struct sockaddr_in6);
#endif
    remote_addr.s6.sin6_port = htons(port);
  }else ASSERT(false);
  return remote_addr.ss;
}

int udp_connect(struct sockaddr_storage * local, struct sockaddr_storage * remote, bool reuse){
  int fd;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
  } remote_addr, local_addr;
  char * buf = "Hello?";
  char addrbuf[INET6_ADDRSTRLEN];
  socklen_t len;
  int reading = 0;
  struct timeval timeout;

  //remote_addr.ss = udp_get_addr(remote_address, port);
  fd = socket(local->ss_family, SOCK_DGRAM, 0);
  if (fd < 0) return fd;
  int on = 1, off = 0;
  if(reuse){
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, (socklen_t) sizeof(on));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*) &on, (socklen_t) sizeof(on));
#endif
  }
  
  //local_addr.ss = udp_get_addr(local_address, port);

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
