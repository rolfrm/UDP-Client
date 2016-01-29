#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <iron/mem.h>
#include <iron/types.h>
#include <iron/log.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_share_delete.h"

const char * udpc_delete_service_name = "UDPC_DELETE";


typedef struct{
  char * header;
  char * file_to_delete;
}delete_item;

static void * serialize_delete_item(delete_item item, size_t * out_size){
  void * buffer = NULL;
  size_t size = 0;
  udpc_pack_string(item.header, &buffer, &size);
  udpc_pack_string(item.file_to_delete, &buffer, &size);
  *out_size = size;
  return buffer;
}

static bool deserialize_delete_item(void * buffer, size_t size, delete_item * out_item){
  char * header = udpc_unpack_string2(&buffer, &size);
  if(header == NULL) return false;
  char * file  = udpc_unpack_string2(&buffer, &size);
  if(file == NULL) return false;
  out_item->header = header;
  out_item->file_to_delete = file;
  return true;
}


bool udpc_delete_serve(udpc_connection * con, char * basedir){
  char buffer[1000];
  int r = udpc_peek(con, buffer, sizeof(buffer));
  if(r < 0)
    return false;
  delete_item item;
  if(!deserialize_delete_item(buffer, sizeof(buffer), &item))
    return false;
  if(strcmp(item.header, udpc_delete_service_name) != 0)
    return false;
  udpc_read(con, buffer, sizeof(buffer));
  char filepathbuffer[1000];
  sprintf(filepathbuffer, "%s/%s",basedir, item.file_to_delete);
  logd("Deleting %s\n", filepathbuffer);
  remove(filepathbuffer);
  udpc_write(con, buffer, r);
  return true;
}

void udpc_delete_client(udpc_connection * con, char * file_to_delete){
  logd("sending delete %s\n", file_to_delete);
  delete_item item = {(char *) udpc_delete_service_name, file_to_delete};
  size_t buffer_size;
  void * buffer = serialize_delete_item(item, &buffer_size);
 start:
  udpc_write(con, buffer, buffer_size);

  char buffer2[buffer_size];
  int read = udpc_peek(con, buffer2, buffer_size);
  if(read == -1)
    goto start;
  udpc_read(con, buffer2, buffer_size);
  delete_item item2;
  bool parsable = deserialize_delete_item(buffer2, buffer_size, &item2);
  ASSERT(parsable);
  ASSERT(strcmp(item2.header, udpc_delete_service_name) == 0);
  ASSERT(strcmp(item2.file_to_delete, file_to_delete) == 0);
  dealloc(buffer);
}

