#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <iron/mem.h>
#include <iron/types.h>
void udpc_pack(const void * data, size_t data_len, void ** buffer, size_t * buffer_size){
  *buffer = ralloc(*buffer, *buffer_size + data_len);
  memcpy(*buffer + *buffer_size, data, data_len);
  *buffer_size += data_len;
}

void udpc_pack_int(int value, void ** buffer, size_t * buffer_size){
  udpc_pack(&value, sizeof(value), buffer, buffer_size);
}

void udpc_pack_size_t(size_t value, void ** buffer, size_t * buffer_size){
  udpc_pack(&value, sizeof(value), buffer, buffer_size);
}

void udpc_pack_u8(u8 value, void ** buffer, size_t * buffer_size){
  udpc_pack(&value, sizeof(value), buffer, buffer_size);
}

void udpc_unpack(void * dst, size_t size, void ** buffer){
  memcpy(dst, *buffer, size);
  *buffer = *buffer + size;
}

int udpc_unpack_int(void ** buffer){
  int value = 0;
  udpc_unpack(&value, sizeof(value), buffer);
  return value;
}

u8 udpc_unpack_u8(void ** buffer){
  u8 value = 0;
  udpc_unpack(&value, sizeof(value), buffer);
  return value;
}

size_t udpc_unpack_size_t(void ** buffer){
  size_t value = 0;
  udpc_unpack(&value, sizeof(value), buffer);
  return value;
}



void udpc_pack_string(const void * str, void ** buffer, size_t * buffer_size){
  udpc_pack(str, strlen(str) + 1, buffer, buffer_size);
}

char * udpc_unpack_string(void ** buffer){
  int len = strlen((char *) *buffer);
  char * dataptr = *buffer;
  *buffer += len + 1;
  return dataptr;
}

char * udpc_unpack_string2(void ** buffer, size_t * maxsize){
  void * eos = memchr(*buffer, 0, *maxsize);
  if(eos == NULL)
    return NULL;
  eos += 1; // skip the 0
  char * result = *buffer;
  size_t offset = ((char *)eos) - result;
  *maxsize -= offset;
  *buffer = eos;
  return result;
}

u64 get_rand_u64(){
  u64 rnd;
  u32 x1 = rand();
  u32 x2 = rand();
  u32 * _id = (u32 *) &rnd;
  _id[0] = x1;
  _id[1] = x2;
  return rnd;
}
