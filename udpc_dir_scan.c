#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h> // chdir

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
      list_add((void **)&ds.files, &ds.cnt, &file, sizeof(file));
      ds.cnt--;
      list_add((void **) &ds.md5s, &ds.cnt, &md5, sizeof(md5));
      ds.cnt--;
      list_add((void **) &ds.last_change, &ds.cnt, &st->st_mtime, sizeof(st->st_mtime));
    }
    return 0;
  }
  char * cdir = get_current_dir_name();
  ASSERT(0 == chdir(basedir));
  
  ftw(".", ftwf, 100);
  ASSERT(0 == chdir(cdir));

  return ds;
}


void ensure_directory(const char * filepath){
  char * file1 = strstr(filepath, "/");
  if(file1 != NULL){
    size_t cnt = file1 - filepath;
    char buffer[cnt + 1];
    memset(buffer, 0, cnt +1);
    memcpy(buffer,filepath, cnt);
    mkdir(buffer, 0777);
    char *cdir = get_current_dir_name();
    ASSERT(0 == chdir(buffer));
    ensure_directory(file1 + 1);
    ASSERT(0 == chdir(cdir));
  }
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

bool udpc_md5_compare(udpc_md5 a, udpc_md5 b){
  for(size_t i = 0; i < array_count(a.md5); i++)
    if(a.md5[i] != b.md5[i])
      return false;
  return true;
}

void dirscan_clean(dirscan * _dirscan){
  for(size_t i = 0; i < _dirscan->cnt; i++)
    dealloc(_dirscan->files[i]);
  dealloc(_dirscan->files);
  dealloc(_dirscan->md5s);
  dealloc(_dirscan->last_change);
  memset(_dirscan, 0, sizeof(_dirscan[0]));
}

static void * dirscan_to_buffer(dirscan _dirscan, size_t * size){
  void * buffer = NULL;
  udpc_pack_size_t(_dirscan.cnt, &buffer, size);
  for(size_t i = 0; i < _dirscan.cnt; i++)
    udpc_pack_string(_dirscan.files[i], &buffer, size);
  udpc_pack(_dirscan.md5s, _dirscan.cnt * sizeof(_dirscan.md5s[0]), &buffer, size);
  udpc_pack(_dirscan.last_change, _dirscan.cnt * sizeof(_dirscan.last_change[0]), &buffer, size);

  return buffer;
}

static dirscan dirscan_from_buffer(void * buffer){
  void * ptr = buffer;
  size_t cnt = udpc_unpack_size_t(&ptr);
  char * strs[cnt];
  for(size_t i = 0; i < cnt;i++){
    char * str = udpc_unpack_string(&ptr);
    strs[i] = iron_clone(str, strlen(str) + 1);;
  }
  dirscan out;
  out.cnt = cnt;
  out.files = iron_clone(strs, sizeof(strs));
  out.md5s = alloc0(sizeof(out.md5s[0]) * cnt);
  udpc_unpack(out.md5s, sizeof(out.md5s[0]) * cnt, &ptr);
  out.last_change = alloc0(sizeof(out.last_change[0]) * cnt);
  udpc_unpack(out.last_change, sizeof(out.last_change[0]) * cnt, &ptr);
  return out;
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
    size_t tosend = MIN(send_buf_size, buffer_size);
    
    udpc_write(con, bufptr, tosend);
    bufptr += tosend;
    send_buf_size -= tosend;
    iron_usleep(delay_us);
  }
  iron_usleep(10000);
  udpc_write(con, "ENDENDEND", 10);
}

int udpc_dirscan_client(udpc_connection * con, dirscan * dscan){
  udpc_write(con, udpc_dirscan_service_name, strlen(udpc_dirscan_service_name) + 1);
  void * buffer = NULL;
  size_t size = 0;

  udpc_set_timeout(con, 1000000);
  
  while(true){
    char readbuffer[2000];
    size_t r = 0;
    r = udpc_read(con, &readbuffer, sizeof(readbuffer));
    if(r == 0){
      loge("Unable to read from stream\n");
      return -1;
    }
    if(strcmp("ENDENDEND", readbuffer) == 0)
      break;
    udpc_pack(readbuffer, r, &buffer, &size);
  }
  *dscan = dirscan_from_buffer(buffer);
  dealloc(buffer);
  return 0;
}
