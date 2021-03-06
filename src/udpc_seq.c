#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/time.h>
#include <iron/mem.h>
#include <iron/utils.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_seq.h"


static u64 get_seq_work_id(){
  static u64 seq_work_id;
  static bool initialized = false;
  if(!initialized){
    initialized = true;
    seq_work_id = get_rand_u64();
  }
  return seq_work_id;
}

udpc_seq udpc_setup_seq(udpc_connection * con){
 
  u64 seq_id = get_rand_u64();
  u64 datatosend[] ={ get_seq_work_id(), seq_id};
 start:
  udpc_write(con, datatosend, sizeof(datatosend));
  char buffer[1024];
  int r = udpc_peek(con, buffer, sizeof(buffer));
  for(int i = 0; i < 3 && r == -1; i++)
    r = udpc_peek(con, buffer, sizeof(buffer));
  if(r == -1) goto start;
  ASSERT(r == sizeof(u64) * 2);
  void * buf = buffer;
  u64 nseq = udpc_unpack_size_t(&buf);
  ASSERT(nseq == seq_id);
  u64 other_seq = udpc_unpack_size_t(&buf);
  udpc_seq seq_struct = {con, seq_id, 0, other_seq, 0, timestamp()};
  udpc_read(con, buffer, sizeof(buffer));
  return seq_struct;
}

udpc_seq udpc_setup_seq_peer(udpc_connection * con){
  
  u64 seq_id = get_rand_u64();
  u64 datatosend[2];
  int r = udpc_peek(con, datatosend, sizeof(datatosend));
  ASSERT(sizeof(u64) * 2 == r);
  ASSERT(get_seq_work_id() == datatosend[0]);
  u64 other_seqid = datatosend[1];
  ASSERT(udpc_read(con, datatosend, sizeof(datatosend)) == sizeof(u64) * 2);
  datatosend[0] = other_seqid;
  datatosend[1] = seq_id;
  udpc_write(con, datatosend, sizeof(datatosend));
  return (udpc_seq){con, seq_id, 0, other_seqid, 0, timestamp()};
}

int udpc_seq_read(udpc_seq * con, void * buffer, size_t max_size, u64 * seq_number){
  char nbuffer[max_size + sizeof(u64) * 2];
  int r = udpc_peek(con->con, nbuffer, sizeof(nbuffer));

  if(r == -1) return r;
  if(r < (int)sizeof(u64) * 2) return -2;
  u64 * n = (u64 *) nbuffer;
  
  if(n[0] != con->seq_id)
    return -2;
  if(n[1] < con->seq_cnt){
    return -2;
  }
  con->seq_cnt = n[1];
  memcpy(buffer, nbuffer + sizeof(u64) * 2, r - sizeof(u64) * 2);
  udpc_read(con->con, nbuffer, sizeof(nbuffer));
  *seq_number = con->seq_cnt;
  con->last_msg_time = timestamp();
  return r - sizeof(u64) * 2;
}

void udpc_seq_write(udpc_seq * con, const void * buffer, size_t size){
  char nbuffer[size + sizeof(u64) * 2];
  u64 * n = (u64 *) nbuffer;
  n[0] = con->seq_other_id;
  n[1] = ++(con->seq_other_cnt);
  memcpy(n + 2, buffer, size);
  udpc_write(con->con, n, sizeof(nbuffer));
}

void udpc_conv_load(udpc_conv * conv, udpc_connection * con, u64 seqid){
  conv->con = con;
  conv->seqid = seqid;
}

int udpc_conv_write(udpc_conv * conv, void * data, size_t size){
  char nbuffer[size + sizeof(u64)];
  u64 * n = (u64 *) nbuffer;
  n[0] = conv->seqid;
  memcpy(n + 1, data, size);
  udpc_write(conv->con, n, sizeof(nbuffer));
  return 0;
}

int udpc_conv_read(udpc_conv * conv, void * buffer, size_t buffer_size){
  char nbuffer[buffer_size + sizeof(u64)];
  int r = udpc_peek(conv->con, nbuffer, sizeof(nbuffer));
  
  if(r == -1) return -1;
  if(r < (int)sizeof(u64)) return -2;
  u64 * n = (u64 *) nbuffer;
  if(n[0] != conv->seqid)
    return -2;
  //logd("Conv read: %i\n", r);
  memcpy(buffer, nbuffer + sizeof(u64), r - sizeof(u64));
  udpc_read(conv->con, nbuffer, sizeof(nbuffer));
  conv->last_msg_time = timestamp();
  return r - sizeof(u64);
}

udpc_connection_stats get_stats(){
  udpc_connection_stats stats;
  stats.opt_mtu_size = 1400;
  stats.delay_us = 50;
  stats.rtt_peak_us = 50;
  stats.rtt_mean_us = 50;
  return stats;
}

typedef enum{
  UDPC_TRANSMISSION_GET_ALL = 11,
  UDPC_TRANSMISSION_MEASURE_RTT,
  UDPC_TRANSMISSION_GET_STATS,
  UDPC_TRANSMISSION_GET_ITEM,
  UDPC_TRANSMISSION_SEQ,
  UDPC_TRANSMISSION_END
}udpc_transmission_command;

int udpc_receive_transmission(udpc_connection * con, udpc_connection_stats * stats, u64 service_id,
			      int (* handle_chunk)(const transmission_data * tid, const void * chunk,
						   size_t chunk_id, size_t chunk_size, void * userdata),
			      void * userdata){
  udpc_set_timeout(con, 1000000 + stats->rtt_peak_us * 10);
  udpc_conv conv;

  udpc_conv_load(&conv, con, service_id);
  void * buffer = NULL;
  size_t buffer_size = 0;
 
  udpc_pack_u8(UDPC_TRANSMISSION_GET_STATS, &buffer, &buffer_size);
 transmission_start:; 
  udpc_conv_write(&conv, buffer, buffer_size);
  {
    char buffer[1500];
    int peek = udpc_peek(conv.con, buffer, sizeof(buffer));
    if(peek == -1)
      goto transmission_start;

  }

  transmission_data data;
  int peek2 = udpc_conv_read(&conv, &data, sizeof(data));
  if(peek2 == -1) goto transmission_start;
  if(peek2 == -2) return -1;
  ASSERT(peek2 == sizeof(data));
  buffer_size = 0;
  u64 num_chunks = data.total_size / data.chunk_size
    + (((data.total_size % data.chunk_size ) == 0 || data.total_size == 0) ? 0 : 1);
  bool * received_seqs = alloc0(sizeof(bool) * num_chunks);
  if(num_chunks == 0)
    goto end_seq;
 get_all:
  udpc_pack_u8(UDPC_TRANSMISSION_GET_ALL, &buffer, &buffer_size);
  udpc_conv_write(&conv, buffer, buffer_size);
  {
    char buffer[1600];
    int peek = udpc_peek(conv.con, buffer, sizeof(buffer));
    if(peek == -1)
      goto get_all;
  }
 read_seqs:
  while(true){
    char buffer[1600] = {0};
    int read = udpc_conv_read(&conv, buffer, sizeof(buffer));
    if(read == -1) break; // probably last packet is sent.
    if(read < 0) return read;
    if(read < (int)sizeof(u8)) ERROR("Invalid read\n");

    void * ptr = buffer;
    udpc_transmission_command cmd = (udpc_transmission_command) udpc_unpack_u8(&ptr);
    switch(cmd){
    case UDPC_TRANSMISSION_MEASURE_RTT :
      udpc_conv_write(&conv, buffer, read);
      continue;
    case UDPC_TRANSMISSION_GET_ALL:
    case UDPC_TRANSMISSION_SEQ:
      {
	u8 control = udpc_unpack_u8(&ptr);
	ASSERT(control == 123 || control == 124);
	size_t seq_nr = udpc_unpack_size_t(&ptr);
	handle_chunk(&data, ptr, seq_nr, read - sizeof(u8) - sizeof(u64), userdata);
	received_seqs[seq_nr] = true;
	if(seq_nr == num_chunks - 1)
	  goto end_seq;
      }
      break;
      
    default:
      ERROR("This should not happen: Cmd = %i\n", cmd);   
    }
  }
 end_seq:;
  
  u64 j = 0;
  int missing_chunk_seq[300];
  for(u64 i = 0; i < num_chunks && j < array_count(missing_chunk_seq); i++){
    if(received_seqs[i] == false){
      missing_chunk_seq[j++] = i;
    }
  }
  if(j > 0){
    void * buffer = NULL;
    size_t buffer_size = 0;
    udpc_pack_u8(UDPC_TRANSMISSION_SEQ, &buffer, &buffer_size);
    udpc_pack_size_t(j, &buffer, &buffer_size);
    udpc_pack(missing_chunk_seq, j * sizeof(int), &buffer, &buffer_size);
  sendpkg:
    
    udpc_conv_write(&conv, buffer, buffer_size);
    char peekbuf[1500];
    int read = udpc_peek(conv.con, peekbuf, sizeof(peekbuf));
    if(read == -1){
      goto sendpkg;
    }
    goto read_seqs;
  }
  
  free(received_seqs);
  received_seqs = NULL;
  { // sem
    void * buffer = NULL;
    size_t buffer_size = 0;
    udpc_pack_u8(UDPC_TRANSMISSION_END, &buffer, &buffer_size);
    udpc_conv_write(&conv, buffer, buffer_size);
  }
  return 0;
}

int udpc_send_transmission(udpc_connection * con, udpc_connection_stats * stats, u64 service_id,
			   size_t total_size, size_t chunk_size,
			   int (* send_chunk)(const transmission_data * tid, void * chunk,
					      size_t chunk_id, size_t chunk_size, void * userdata),
			   void * userdata){
  udpc_set_timeout(con, 1000000);
  udpc_conv conv;
  udpc_conv_load(&conv, con, service_id);
  transmission_data data = { total_size, chunk_size};
  size_t chunk_cnt = total_size / chunk_size + (total_size % chunk_size == 0 || total_size == 0 ? 0 : 1);
  while(true){
    char buffer[1600];
    int r = udpc_conv_read(&conv, buffer, sizeof(buffer));
    //logd("TX: %i\n", r);
    if(r == -1) return -1;
    if(r == -2) {
      udpc_peek(conv.con, buffer, sizeof(buffer));
      return 0; // might actually be ok.
    }
    ASSERT(r != 0);
    void * ptr = buffer;
    udpc_transmission_command cmd = (udpc_transmission_command) udpc_unpack_u8(&ptr);
    switch(cmd){
    case UDPC_TRANSMISSION_GET_STATS:
      udpc_conv_write(&conv, &data, sizeof(data));
      break;
    case UDPC_TRANSMISSION_GET_ALL:

      for(size_t chunk_nr = 0; chunk_nr < chunk_cnt; chunk_nr++){
	size_t offset = chunk_nr * chunk_size;
	size_t tosend = MIN(chunk_size, total_size - offset);
	char buffer[tosend + 1 + sizeof(tosend) + 1];
	buffer[0] = cmd;
	buffer[1] = 124;
	memcpy(buffer + 2, &chunk_nr, sizeof(tosend));
	int ok = send_chunk(&data, buffer + 2 + sizeof(tosend), chunk_nr, tosend, userdata);
	ASSERT(ok == 0);
	udpc_conv_write(&conv, buffer, sizeof(buffer));
	iron_usleep(stats->delay_us);
      }
      break;
    case UDPC_TRANSMISSION_MEASURE_RTT:
      udpc_conv_write(&conv, buffer, r);
      break;
    case UDPC_TRANSMISSION_SEQ:
      {
	u64 cnt = udpc_unpack_size_t(&ptr);
	int missing_chunk_seq[cnt];
	udpc_unpack(missing_chunk_seq, cnt * sizeof(int), &ptr);
	for(u64 i = 0; i < cnt; i++){
	  size_t chunk_nr = missing_chunk_seq[i];
	  size_t offset = chunk_nr * chunk_size;
	  size_t tosend = MIN(chunk_size, total_size - offset);
	  char buffer[tosend + 1 + sizeof(tosend) + 1];
	  buffer[0] = cmd;
	  buffer[1] = 123;
	  memcpy(buffer + 2, &chunk_nr, sizeof(tosend));
	  int ok = send_chunk(&data, buffer + 2 + sizeof(tosend), chunk_nr, tosend, userdata);
	  ASSERT(ok == 0);
	  udpc_conv_write(&conv, buffer, sizeof(buffer));
	  iron_usleep(stats->delay_us);
	}
      }
      break;
    case UDPC_TRANSMISSION_END:
      goto end;
    default:
      ERROR("invalid cmd\n");
    }
  }
 end:;
  return 0;
}
