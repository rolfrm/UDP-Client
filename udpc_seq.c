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

#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_seq.h"

static u64 get_rand_u64(){
  u64 rnd;
  u32 x1 = rand();
  u32 x2 = rand();
  u32 * _id = (u32 *) &rnd;
  _id[0] = x1;
  _id[1] = x2;
  return rnd;
}

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
  udpc_write(con, datatosend, sizeof(datatosend));
  char buffer[1024];
  int r = udpc_peek(con, buffer, sizeof(buffer));
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
