// simple library to userspace UDPC lib to test connection speed.
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
#include "udpc_dir_scan.h"

#include "udpc_send_file.h"

const char * udpc_file_serve_service_name = "UDPC_FILE_SERVE";
typedef struct{
  int start;
  int cnt;
}missing_seq;

static void _send_file(udpc_connection * c2, udpc_connection_stats * stats, char * filepath, u64 transmission_id){
  FILE * file = fopen(filepath, "r");
  if(file == NULL){
    logd("NO FILE %s\n", filepath);
    return;
  }

  fseek(file,0,SEEK_END);
  size_t size = ftell(file);
  
  int handle_chunk(const transmission_data * tid, void * _chunk,
		   size_t chunk_id, size_t chunk_size, void * userdata){
    UNUSED(tid); UNUSED(userdata);
    size_t offset =  chunk_id * tid->chunk_size;
    fseek(file, offset, SEEK_SET);
    fread(_chunk, chunk_size, 1, file);
    return 0;
  }
  
  udpc_send_transmission( c2, stats, transmission_id, size,
			  stats->opt_mtu_size - 20, handle_chunk, NULL);
  fclose(file);
}

void _receive_file(udpc_connection * c2, udpc_connection_stats * stats, char * filepath, u64 transmission_id){
  logd("Sending file: %s\n", filepath);
  ensure_directory(filepath);
  FILE * file = fopen(filepath, "w");
  ASSERT(file != NULL);
  int handle_chunk(const transmission_data * tid, const void * _chunk,
		     size_t chunk_id, size_t chunk_size, void * userdata){
    UNUSED(userdata); UNUSED(tid);
    size_t offset = chunk_id * tid->chunk_size;
    fseek(file, offset, SEEK_SET);
    fwrite(_chunk, chunk_size, 1, file);
    return 0;
  }
  int status = udpc_receive_transmission(c2, stats, transmission_id,
					 handle_chunk, NULL);
  fclose(file);
  UNUSED(status);
}

void udpc_file_serve(udpc_connection * c2, udpc_connection_stats * stats, char * dir){
  char buf2[1000]; 
  int r = udpc_read(c2,buf2, sizeof(buf2));
  if(r < 0)
    return;
  void * ptr = buf2;
  char * service = udpc_unpack_string(&ptr);
  ASSERT(strcmp(service, udpc_file_serve_service_name) == 0);
  int delay = udpc_unpack_int(&ptr);
  int buffer_size = udpc_unpack_int(&ptr);
  stats->delay_us = delay;
  stats->opt_mtu_size = buffer_size;
  char * filepath = udpc_unpack_string(&ptr);
  u64 transmission_id = udpc_unpack_size_t(&ptr);
  int rcv = udpc_unpack_int(&ptr);
  char filepathbuffer[1000];
  memset(filepathbuffer, 0, sizeof(filepathbuffer));
  sprintf(filepathbuffer, "%s/%s",dir, filepath);
  if(rcv == 1){
    _receive_file(c2, stats, filepathbuffer, transmission_id);
  }else{
    _send_file(c2, stats, filepathbuffer, transmission_id);
  }
}

void udpc_file_client(udpc_connection * con, udpc_connection_stats * stats, char * remote_file_path, char * local_file_path){
  u64 transmission_id = get_rand_u64();
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_file_serve_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(stats->delay_us, &outbuffer, &buffer_size);
  udpc_pack_int(stats->opt_mtu_size, &outbuffer, &buffer_size);
  udpc_pack_string(remote_file_path, &outbuffer, &buffer_size);
  udpc_pack_size_t(transmission_id, &outbuffer, &buffer_size);
  udpc_pack_int(0, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);
  dealloc(outbuffer);
  _receive_file(con, stats, local_file_path, transmission_id);
}

void udpc_file_client2(udpc_connection * con, udpc_connection_stats * stats, char * remote_file_path, char * local_file_path){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  u64 transmission_id = get_rand_u64();
  udpc_pack_string(udpc_file_serve_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(stats->delay_us, &outbuffer, &buffer_size);
  udpc_pack_int(stats->opt_mtu_size, &outbuffer, &buffer_size);
  udpc_pack_string(remote_file_path, &outbuffer, &buffer_size);
  udpc_pack_size_t(transmission_id, &outbuffer, &buffer_size);
  udpc_pack_int(1, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);
  dealloc(outbuffer);
  _send_file(con, stats, local_file_path, transmission_id);
}
