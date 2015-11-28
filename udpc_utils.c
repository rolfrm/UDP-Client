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

void udpc_pack(const void * data, size_t data_len, void ** buffer, size_t * buffer_size){
  *buffer = ralloc(*buffer, *buffer_size + data_len);
  memcpy(*buffer + *buffer_size, data, data_len);
  *buffer_size += data_len;
}

void udpc_pack_int(int value, void ** buffer, size_t * buffer_size){
  udpc_pack(&value, sizeof(int), buffer, buffer_size);
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

void udpc_pack_string(const void * str, void ** buffer, size_t * buffer_size){
  udpc_pack(str, strlen(str) + 1, buffer, buffer_size);
}

char * udpc_unpack_string(void ** buffer){
  int len = strlen((char *) *buffer);
  char * dataptr = *buffer;
  *buffer += len + 1;
  return dataptr;
}
