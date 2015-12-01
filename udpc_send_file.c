// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
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
#include "udpc_dir_scan.h"

#include "udpc_send_file.h"

const char * udpc_file_serve_service_name = "UDPC_FILE_SERVE";
typedef struct{
  int start;
  int cnt;
}missing_seq;

static void _send_file(udpc_connection * c2, char * filepath, int delay, int buffer_size){
  FILE * file = fopen(filepath, "r");
  if(file == NULL){
    udpc_write(c2, "ENDENDEND", 10);
    udpc_close(c2);
    return;
  }
  { // send file and buffer size
    fseek(file, 0, SEEK_END);
    size_t file_size = ftello(file);
    fseek(file, 0, SEEK_SET);
    void * bufptr = NULL;
    size_t bufptr_size = 0;
    udpc_pack_size_t(file_size, &bufptr, &bufptr_size);
    udpc_pack_size_t(file_size / (buffer_size - sizeof(int)), &bufptr, &bufptr_size);
    udpc_write(c2, bufptr, bufptr_size);
  }
  
  char buffer[buffer_size];
  for(int i = 0; true; i++){
    memcpy(buffer, &i, sizeof(i));
    size_t read = fread(buffer + sizeof(i), 1, buffer_size - sizeof(i), file);
    if(delay > 0)
      iron_usleep(delay);
    if(read == 0)
      break;
    udpc_write(c2, buffer, read + sizeof(i));
  }
  iron_usleep(delay * 10);
  udpc_write(c2, "ENDENDEND", 10);
  iron_usleep(10000);
  delay = 10000;
  while(true){
    size_t r = udpc_read(c2, buffer, sizeof(buffer));
    ASSERT(r > 0 &&  r != (size_t)-1);
    logd("Missing: %i\n", r);
    if(r >= 3 && strncmp(buffer, "OK",3) == 0)
      break;
    void * ptr = buffer;
    missing_seq mis;
    udpc_unpack(&mis, sizeof(missing_seq), &ptr);
    logd("Got missing seq %i %i\n", mis.start, mis.cnt);
    off_t offset = mis.start * (buffer_size - sizeof(int));
    size_t cnt = mis.cnt;
    fseeko(file, offset, SEEK_SET);
    for(size_t _i = 0; _i < cnt; _i++){
      int i = mis.start + i;
      memcpy(buffer, &i, sizeof(i));
      size_t read = fread(buffer + sizeof(i), 1, buffer_size - sizeof(i), file);
      //logd("sending chunk: %i %i\n", read, i);
      if(delay > 0)
	iron_usleep(delay);
      udpc_write(c2, buffer, read + sizeof(i));
    }
    iron_usleep(delay * 10);
    udpc_write(c2, "ENDENDEND", 10);
  }
  fclose(file);
}

void _receive_file(udpc_connection * c2, char * filepath, int buffer_size){
  ensure_directory(filepath);
  FILE * file = fopen(filepath, "w");
  ASSERT(file != NULL);
  udpc_set_timeout(c2, 1000000);
  char buffer[buffer_size];
  size_t file_size;
  int num_chunks;
  { // get file size/number of chunks.
    void *bufptr = buffer;
    udpc_read(c2, buffer, sizeof(buffer));
    file_size = udpc_unpack_size_t(&bufptr);
    num_chunks = udpc_unpack_int(&bufptr);
  }
  logd("File size: %i, Number of chunks: %i\n", file_size, num_chunks);
  int current = -1;
  
  missing_seq * missing = NULL;
  size_t missing_cnt = 0;
  while(true){
    size_t r = udpc_read(c2, buffer, sizeof(buffer));
    if(r == 0) break;
    if(strcmp("ENDENDEND", buffer) == 0)
      break;
    void * ptr = buffer;
    int seq = udpc_unpack_int(&ptr);
    fwrite(ptr,1,r - sizeof(seq), file);
    if(current + 1 != seq){
      // handle missed package
      missing_seq seq2 = {current + 1, seq - current - 1};
      udpc_pack(&seq2, sizeof(seq2), (void **) &missing, &missing_cnt);
    }
    current = seq;
  }
  
  while(missing_cnt != 0){
    missing_cnt /= sizeof(missing_seq);
    size_t missing_cnt2 = missing_cnt;
    logd("Missing: %i\n", missing_cnt2);
    for(size_t i = 0; i < missing_cnt; i++){
      logd("%i: %i %i\n", i, missing[i].start, missing[i].cnt);
    }
    missing_seq * missing2 = missing;
    missing_cnt = 0;
    missing = NULL;
    for(size_t i = 0; i < missing_cnt2; i++){
      missing_seq m = missing2[i];
      logd("Sending missing seq %i/%i %i %i\n",i,missing_cnt2, m.start, m.cnt);
      udpc_write(c2, &m, sizeof(m));
      iron_usleep(1000);
      int current = missing2[i].start - 1;
      while(true){
	size_t r = udpc_read(c2, buffer, buffer_size);
	if(r == 10 && strncmp(buffer, "ENDENDEND", 0) == 0){
	  logd("Got end..\n");
	  break;
	}
	logd("received chunk.. %i\n", r);
	void * ptr = buffer;
	int seq = udpc_unpack_int(&ptr);
	off_t offset = seq * (buffer_size - sizeof(int));
	fseeko(file, offset, SEEK_SET);
	fwrite(buffer, 1, r, file);
	if(seq != current + 1){
	  logd("This happens.. %i %i\n", seq, current);
	  //missing_seq seq2 = {current + 1, seq - current - 1};
	  //udpc_pack(&seq2, sizeof(seq2), (void **) &missing, &missing_cnt);
	}
      }
    }
    if(missing2 != NULL)
      dealloc(missing2);
  }
  udpc_write(c2, "OK", sizeof("OK"));
  fclose(file);
}

void udpc_file_serve(udpc_connection * c2, void * ptr){
  if(ptr == NULL){
    char buf2[1000]; 
    size_t r = 0;
    while(r == 0)
      r = udpc_read(c2,buf2, sizeof(buf2));
    ptr = buf2;
    char * code = udpc_unpack_string(&ptr);
    if(strcmp(code, udpc_file_serve_service_name) != 0){
      return;
    }
  }else{
    char * code = udpc_unpack_string(&ptr);
    ASSERT(strcmp(code, udpc_file_serve_service_name) == 0);
  }
  
  int delay = udpc_unpack_int(&ptr);
  int buffer_size = udpc_unpack_int(&ptr);
  char * filepath = udpc_unpack_string(&ptr);
  int rcv = udpc_unpack_int(&ptr);
  if(rcv == 1){
    _receive_file(c2, filepath, buffer_size);
  }else{
    _send_file(c2, filepath, delay, buffer_size);
  }
}

void udpc_file_client(udpc_connection * con, int delay, int bufsize, char * remote_file_path, char * local_file_path){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_file_serve_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(delay, &outbuffer, &buffer_size);
  udpc_pack_int(bufsize, &outbuffer, &buffer_size);
  udpc_pack_string(remote_file_path, &outbuffer, &buffer_size);
  udpc_pack_int(0, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);
  dealloc(outbuffer);
  _receive_file(con, local_file_path, bufsize);
}

void udpc_file_client2(udpc_connection * con, int delay, int bufsize, char * remote_file_path, char * local_file_path){
  void * outbuffer = NULL;
  size_t buffer_size = 0;
  udpc_pack_string(udpc_file_serve_service_name, &outbuffer, &buffer_size);
  udpc_pack_int(delay, &outbuffer, &buffer_size);
  udpc_pack_int(bufsize, &outbuffer, &buffer_size);
  udpc_pack_string(remote_file_path, &outbuffer, &buffer_size);
  udpc_pack_int(1, &outbuffer, &buffer_size);
  udpc_write(con, outbuffer, buffer_size);
  dealloc(outbuffer);
  _send_file(con, local_file_path, delay, bufsize);
}
