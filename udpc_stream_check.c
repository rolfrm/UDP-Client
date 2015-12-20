// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <iron/types.h>
#include <iron/log.h>
#include <iron/time.h>

#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_stream_check.h"
#include "udpc_seq.h"
const char * udpc_speed_test_service_name = "UDPC_SPEED_TEST";

void udpc_speed_serve(udpc_connection * c2, void * ptr){
  if(ptr == NULL){
    char buf2[1000]; 
    int r = udpc_read(c2,buf2, sizeof(buf2));
    ASSERT(r != -1);
    ptr = buf2;
    char * code = udpc_unpack_string(&ptr);
    logd("Test: %s\n", code);
    if(strcmp(code, udpc_speed_test_service_name) != 0){
      udpc_close(c2);
      return;
    }
  }else{
    char * code = udpc_unpack_string(&ptr);
    ASSERT(strcmp(code, udpc_speed_test_service_name) == 0);
  }
  udpc_write(c2, "OK", sizeof("OK"));
  udpc_seq s = udpc_setup_seq_peer(c2);
  
  
  int delay = udpc_unpack_int(&ptr);
  int buffer_size = udpc_unpack_int(&ptr);
  int count = udpc_unpack_int(&ptr);
  char buffer[buffer_size];
  for(int i = 0; i < count; i++){
    memcpy(buffer, &i, sizeof(i));
    udpc_write(c2, buffer, buffer_size);
    if(delay > 0)
      iron_usleep(delay);
  }
  iron_usleep(10000);
  udpc_write(c2, "ENDENDEND", 10);
}

void udpc_speed_client(udpc_connection * con, int delay, int bufsize, int count, int * out_missed, int * out_missed_seqs){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_speed_test_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(delay, &outbuffer, &buffer_size);
  udpc_pack_int(bufsize, &outbuffer, &buffer_size);
  udpc_pack_int(count, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);

  int r = udpc_read(con, outbuffer, buffer_size);
  udpc_seq s = udpc_setup_seq(con);
  if(r == -1){
    // packet lost. try again.
    udpc_write(con, outbuffer, buffer_size);
    r = udpc_read(con, outbuffer, buffer_size);
    if(r == -1){
      loge("Connection timed out\n");
      free(outbuffer);
      return;
    }
  }
  // it should have returned "OK" now.
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
    if(current + 1 != seq){
      *out_missed += seq - current - 1;
      *out_missed_seqs += 1;
    }
    current = seq;
  }
}
