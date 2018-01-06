#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <iron/log.h>
#include <iron/process.h>
#include "udp.h"

struct sockaddr_storage udp_get_addr(const char * remote_address, int port){
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
static int opened_closed = 0;
static int closed_ports[1024];

static iron_mutex mut = {0};
void take_udp_mutex(){
  if(mut.data == NULL){
    mut = iron_mutex_create();
  }
  iron_mutex_lock(mut);
  logd("TAKE MUTEX\n");
}

void release_udp_mutex(){
  iron_mutex_unlock(mut);
  logd("Release MUTEX\n");
}
int udp_connect(struct sockaddr_storage * local, struct sockaddr_storage * remote, bool reuse){
  take_udp_mutex();
  int fd = socket(local->ss_family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
  if (fd < 0) {
    release_udp_mutex();
    return fd;
  }
  if(closed_ports[fd] == 1){
    ERROR("CONNECT: PORT ALREADY OPEN %i\n", fd);
  }
  int on = 1;
  if(reuse){
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, (socklen_t) sizeof(on));
    //#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*) &on, (socklen_t) sizeof(on));
    //#endif
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
  opened_closed += 1;
  logd("CONNECTED: %i\n", fd);
  
  closed_ports[fd] = 1;

  release_udp_mutex();  
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
  take_udp_mutex();
  int fd = socket(local->ss_family, SOCK_DGRAM| SOCK_CLOEXEC, IPPROTO_UDP);
  if(fd < 0){
    release_udp_mutex();
    return fd;
  }
  logd("OPENED: %i\n", fd);
  if(closed_ports[fd] == 1)
    ERROR("OPEN: PORT ALREADY OPEN %i\n", fd);
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void*) &on, (socklen_t) sizeof(on));
  //#ifdef SO_REUSEPORT
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const void*) &on, (socklen_t) sizeof(on));
  //#endif
  if (local->ss_family == AF_INET) {
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in));
  } else {
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&off, sizeof(off));
    bind(fd, (const struct sockaddr *) local, sizeof(struct sockaddr_in6));
  }
  opened_closed += 1;

  closed_ports[fd] = 1;
  release_udp_mutex();
  return fd;
}
void iron_sleep(double);
void udp_close(int fd){
  take_udp_mutex();
  if(closed_ports[fd] == 0)
    ERROR("PORT NOT OPEN");
  ASSERT(fd != 0);
  //int ok = shutdown(fd, 0);
  //logd("OK?? %i\n", ok);
  //flush(fd);
  
  opened_closed -= 1;

  closed_ports[fd] = 0;
  logd("CLOSED: %i %i\n", opened_closed, fd);
  while(!close(fd)){
    iron_sleep(0.001);
  }
  release_udp_mutex();

}
