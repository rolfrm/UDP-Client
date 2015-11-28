// simple program to user UDPC to transfer a file between two peers
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "udpc.h"
#include "service_descriptor.h"
#include <stdint.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/mem.h>
#include <iron/time.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGSTOP);
  exit(255);
}

bool should_close = false;
void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}

static void pack(const void * data, size_t data_len, void ** buffer, size_t * buffer_size){
  *buffer = ralloc(*buffer, *buffer_size + data_len);
  memcpy(*buffer + *buffer_size, data, data_len);
  *buffer_size += data_len;
}

static void pack_int(int value, void ** buffer, size_t * buffer_size){
  pack(&value, sizeof(int), buffer, buffer_size);
}

static void unpack(void * dst, size_t size, void ** buffer){
  memcpy(dst, *buffer, size);
  *buffer = *buffer + size;
}

static int unpack_int(void ** buffer){
  int value = 0;
  unpack(&value, sizeof(value), buffer);
  return value;
}

static void pack_string(const void * str, void ** buffer, size_t * buffer_size){
  pack(str, strlen(str) + 1, buffer, buffer_size);
}

static char * unpack_string(void ** buffer){
  int len = strlen((char *) *buffer);
  char * dataptr = *buffer;
  *buffer += len + 1;
  return dataptr;
}

const char * service_name = "UDPC_SPEED_TEST";

void udpc_speed_serve(udpc_connection * c2){
  char buf2[32]; //or max MTU size
  size_t r = 0;
  while(r == 0)
    r = udpc_read(c2,buf2, sizeof(buf2));
  void * ptr = buf2;
  char * code = unpack_string(&ptr);
  if(strcmp(code, service_name) != 0){
    udpc_close(c2);
    return;
  }

  int delay = unpack_int(&ptr);
  int buffer_size = unpack_int(&ptr);
  int count = unpack_int(&ptr);
  char buffer[buffer_size];
  for(int i = 0; i < count; i++){
    memcpy(buffer, &i, sizeof(i));
    udpc_write(c2, buffer, buffer_size);
    if(delay > 0)
      usleep(delay);
  }
  iron_usleep(10000);
  udpc_write(c2, "ENDENDEND", 10);
  udpc_close(c2);
}

void udpc_speed_client(udpc_connection * con, int delay, int bufsize, int count, int * out_missed, int * out_missed_seqs){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  pack_string(service_name, &outbuffer, &buffer_size);
  pack_int(delay, &outbuffer, &buffer_size);
  pack_int(bufsize, &outbuffer, &buffer_size);
  pack_int(count, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);
  free(outbuffer);
  char buffer[bufsize];
  int current = -1;
  while(true){
    size_t r = 0;
    while(r == 0)
      r = udpc_read(con, buffer, sizeof(buffer));

    if(strcmp("ENDENDEND", buffer) == 0)
      break;
    void * ptr = buffer;
    int seq = unpack_int(&ptr);
    if(current + 1 != seq){
      *out_missed += seq - current - 1;
      *out_missed_seqs += 1;
    }
    current = seq;
  }
  
  udpc_close(con);
}

// UDPC Speed test
// usage:
// SERVER: udpc_get name@server:service
// CLIENT: udpc_get name@server:service delay [buffer_size] [package-count]
int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      udpc_speed_serve(c2);
    }
    udpc_logout(con);
  }else if(argc > 2){

    int delay = 10;
    int bufsize = 1500;
    int count = 10000;
    sscanf(argv[2],"%i", &delay);
    if(argc > 3)
      sscanf(argv[3],"%i", &bufsize);
    if(argc > 4)
      sscanf(argv[4],"%i", &count);
    logd("Delay: %i, buffer size: %i count: %i\n", delay, bufsize, count);
    udpc_connection * con = udpc_connect(argv[1]);
    if(con != NULL){
      int missed = 0, missed_seqs = 0;
      u64 ts1 = timestamp();
      udpc_speed_client(con, delay, bufsize, count, &missed, &missed_seqs);
      i64 successfull = (count - missed);
      logd("Missed: %i seqs: %i\n", missed, missed_seqs);
      logd("Sent: %llu MB\n",((i64)bufsize * successfull) / 1000000);
      u64 ts2 = timestamp();
      double dt = 1e-6 * (double)(ts2 - ts1);
      logd("in %f s\n", dt);
      logd("Speed: %i MB/s\n", (int)(((i64)bufsize * successfull) / dt/ 1e6) );
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
