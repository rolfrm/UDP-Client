#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

#include <iron/types.h>
#include <iron/log.h>
#include <iron/array.h>
#include <iron/mem.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <openssl/md5.h>
#include <ftw.h>

#include "udpc.h"
#include "udpc_utils.h"
#include "service_descriptor.h"
#include "udpc_dir_scan.h"

const char * udpc_dirscan_service_name = "UDPC_DIRSCAN";

void udpc_print_md5(udpc_md5 md5){
  for(int i = 0; i < MD5_DIGEST_LENGTH; i++){
    logd("%2X ",  md5.md5[i]);
  }
}

dirscan scan_directories(const char * basedir){
  dirscan ds = {0};
  int ftwf(const char * filename, const struct stat * st, int ft){
    UNUSED(ft);
    if(S_ISREG(st->st_mode)){
      udpc_md5 md5 = udpc_file_md5(filename);
      char * file = iron_clone(filename, strlen(filename) + 1);
      list_add((void **)&ds.files, &ds.cnt, file, sizeof(file));
      ds.cnt--;
      list_add((void **) &ds.md5s, &ds.cnt, &md5, sizeof(md5));
      ds.cnt--;
      list_add((void **) &ds.last_change, &ds.cnt, &st->st_mtime, sizeof(st->st_mtime));
    }
    return 0;
  }
  ftw(basedir, ftwf, 100);
  return ds;
}

udpc_md5 udpc_file_md5(const char * path){
  FILE * f = fopen(path, "r");
  ASSERT(f != NULL);
  unsigned long r = 0;
  char buffer[1024];
  MD5_CTX md5;
  MD5_Init(&md5);
  while(0 != (r = fread(buffer, 1, sizeof(buffer), f)))
    MD5_Update(&md5, buffer, r);
  udpc_md5 digest;
  MD5_Final(digest.md5, &md5);
  return digest;
}

void dirscan_clean(dirscan * _dirscan){
  dealloc(_dirscan->files);
  dealloc(_dirscan->md5s);
  dealloc(_dirscan->last_change);
  memset(_dirscan, 0, sizeof(_dirscan[0]));
}

static void * dirscan_to_buffer(dirscan _dirscan, size_t * size){
  void * buffer = NULL;
  void * ptr = buffer;
  udpc_pack_size_t(_dirscan.cnt, &ptr, size);
  for(size_t i = 0; i < _dirscan.cnt; i++)
    udpc_pack_string(_dirscan.files[i], &ptr, size);
  udpc_pack(_dirscan.md5s, _dirscan.cnt * sizeof(_dirscan.md5s[0]), &ptr, size);
  udpc_pack(_dirscan.last_change, _dirscan.cnt * sizeof(_dirscan.last_change[0]), &ptr, size);

  return buffer;
}

void udpc_dirscan_serve(udpc_connection * con, dirscan last_dirscan,size_t buffer_size, int delay_us, void * read){
  if(read == NULL){
    char buf2[1000];
    size_t r= 0;
    while(r == 0)
      r = udpc_read(con,buf2, sizeof(buf2));
    read = buf2;
    char * code = udpc_unpack_string(&read);
    if(strcmp(code, udpc_dirscan_service_name) != 0){
      return;
    }

  }else{
    char * code = udpc_unpack_string(&read);
    ASSERT(strcmp(code, udpc_dirscan_service_name) == 0);
  }
  
  size_t send_buf_size = 0;
  void * send_buf = dirscan_to_buffer(last_dirscan, &send_buf_size);
  void * bufptr = send_buf;
  while(send_buf_size > 0){
    size_t tosend = MAX(send_buf_size, buffer_size);
    udpc_write(con, bufptr, tosend);
    bufptr += tosend;
    send_buf_size -= tosend;
    iron_usleep(delay_us);
  }
  iron_usleep(10000);
  udpc_write(con, "ENDENDEND", 10);
}

void udpc_dirscan_client(udpc_connection * con){
  UNUSED(con);
}
