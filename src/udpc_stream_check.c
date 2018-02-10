// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <iron/types.h>
#include <iron/log.h>
#include <iron/time.h>
#include <iron/utils.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_stream_check.h"
#include "udpc_seq.h"
const char * udpc_speed_test_service_name = "UDPC_SPEED_TEST";

void udpc_speed_serve(udpc_connection * c2, void * ptr){
  char buf2[4000]; 
  if(ptr == NULL){
    int r = udpc_read(c2,buf2, sizeof(buf2));
    ASSERT(r != -1);
    ptr = buf2;
    char * code = udpc_unpack_string(&ptr);
    if(strcmp(code, udpc_speed_test_service_name) != 0){
      udpc_close(c2);
      return;
    }
  }else{
    char * code = udpc_unpack_string(&ptr);
    ASSERT(strcmp(code, udpc_speed_test_service_name) == 0);
  }
  
  udpc_seq s = udpc_setup_seq_peer(c2);
  while(true){
    char buffer[4000];
    u64 seq;
    int r = udpc_seq_read(&s, buffer, sizeof(buffer), &seq);
    if(r == -1) continue;
    if(r == -2) return;
    udpc_seq_write(&s, buffer, r);
  }
}

void udpc_speed_client(udpc_connection * con, int delay, int bufsize, int count, int * out_missed, int * out_missed_seqs, double * out_mean_rtt, double * out_peak_rtt){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_speed_test_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(delay, &outbuffer, &buffer_size);
  udpc_pack_int(bufsize, &outbuffer, &buffer_size);
  udpc_pack_int(count, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);

  udpc_seq s = udpc_setup_seq(con);
  
  free(outbuffer);
  char buffer[bufsize];
  int current = 0;
  double mean_rtt = 0.0;
  double peak_rtt = 0.0;
  for(int i = 0; i < count; i++){
    u64 * bufptr = (u64 *) buffer;
    bufptr[0] = timestamp();
    udpc_seq_write(&s, buffer, bufsize);
    u64 seqid = 0;
    int r = udpc_seq_read(&s, buffer, sizeof(buffer), &seqid);
    if(r == -2)
      return;
    if(r == -1)
      continue;
    void * ptr = buffer;
    u64 ts = udpc_unpack_size_t(&ptr);
    u64 _delay = timestamp() - ts;
    mean_rtt += ((double)_delay) / ((double)count);
    peak_rtt = MAX(peak_rtt, _delay);
    int seq = seqid;
    if(current + 1 != seq){
      *out_missed += seq - current - 1;
      *out_missed_seqs += 1;
    }
    current = seq;
    iron_usleep(delay);
  }
  *out_mean_rtt = mean_rtt / 1e6;
  *out_peak_rtt = peak_rtt / 1e6;
}
