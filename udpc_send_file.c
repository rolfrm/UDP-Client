// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <iron/types.h>
#include <iron/log.h>
#include <iron/mem.h>
#include <iron/time.h>

#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_stream_check.h"

#include "udpc_send_file.h"

const char * udpc_file_serve_service_name = "UDPC_FILE_SERVE";

void udpc_file_serve(udpc_connection * c2, void * ptr){
  if(ptr == NULL){
    char buf2[1000]; 
    size_t r = 0;
    while(r == 0)
      r = udpc_read(c2,buf2, sizeof(buf2));
    ptr = buf2;
    char * code = udpc_unpack_string(&ptr);
    if(strcmp(code, udpc_file_serve_service_name) != 0){
      udpc_close(c2);
      return;
    }
  }else{
    char * code = udpc_unpack_string(&ptr);
    ASSERT(strcmp(code, udpc_file_serve_service_name) == 0);
  }

  int delay = udpc_unpack_int(&ptr);
  int buffer_size = udpc_unpack_int(&ptr);
  char * filepath = udpc_unpack_string(&ptr);
  FILE * file = fopen(filepath, "r");
  if(file == NULL){
    udpc_write(c2, "ENDENDEND", 10);
    udpc_close(c2);
    return;
  }
  char buffer[buffer_size];

  for(int i = 0; true; i++){
    memcpy(buffer, &i, sizeof(i));
    size_t read = fread(buffer + sizeof(i), 1, buffer_size - sizeof(i), file);
    if(read == 0)
      break;
    udpc_write(c2, buffer, read + sizeof(i));
    if(delay > 0)
      iron_usleep(delay);
  }
  iron_usleep(10000);
  udpc_write(c2, "ENDENDEND", 10);
}

void udpc_file_client(udpc_connection * con, int delay, int bufsize, char * in_file_path, char * out_file_path){
  FILE * file = fopen(out_file_path, "w");
  ASSERT(file != NULL);
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_file_serve_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(delay, &outbuffer, &buffer_size);
  udpc_pack_int(bufsize, &outbuffer, &buffer_size);
  udpc_pack_string(in_file_path, &outbuffer, &buffer_size);
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
    int seq = udpc_unpack_int(&ptr);
    fwrite(ptr,1,r - sizeof(seq), file);
    if(current + 1 != seq){
      // handle missed package
    }
    current = seq;
  }
}
