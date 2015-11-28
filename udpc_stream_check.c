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

// UDPC sample program.
// usage:
// SERVER: udpc_get name@server:service
// CLIENT: udpc_get name@server:service [command]
int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    logd("Logged in..\n");
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      char buf2[32]; //or max MTU size
      size_t r = 0;
      while(r == 0)
	r = udpc_read(c2,buf2, sizeof(buf2));
      void * ptr = buf2;
      int delay = unpack_int(&ptr);
      int buffer_size = unpack_int(&ptr);
      int count = unpack_int(&ptr);
      logd("Count: %i\n", count);
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
    udpc_logout(con);
  }else if(argc > 2){
    u64 ts1 = timestamp();
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
    void * outbuffer = NULL;
    size_t buffer_size = 0;
    {
      
      pack_int(delay, &outbuffer, &buffer_size);
      pack_int(bufsize, &outbuffer, &buffer_size);
      pack_int(count, &outbuffer, &buffer_size);
      udpc_write(con, outbuffer, buffer_size);
      free(outbuffer);
      char buffer[bufsize];
      int missed = 0;
      int missed_seqs = 0;
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
	  missed += seq - current - 1;
	  missed_seqs += 1;
	  logd("This happens\n");
	}
	current = seq;
	//fwrite(outbuffer, 1, r, stdout);
      }
      logd("Missed: %i\n", missed);
      logd("Sent: %llu MB  %i %i %i\n",((i64)bufsize * (i64)current) / 1000000, bufsize, current, missed_seqs);
      u64 ts2 = timestamp();
      double dt = 1e-6 * (double)(ts2 - ts1);
      logd("in %f s\n", dt);
      logd("Speed: %i MB/s\n", (int)((i64)bufsize * (i64)current / dt/ 1e6) );
      udpc_close(con);
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
