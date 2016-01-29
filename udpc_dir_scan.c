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
#include "udpc_seq.h"
#include "udpc_dir_scan.h"

const char * udpc_dirscan_service_name = "UDPC_DIRSCAN";

void udpc_print_md5(udpc_md5 md5){
  for(int i = 0; i < MD5_DIGEST_LENGTH; i++){
    logd("%2X ",  md5.md5[i]);
  }
}

void udpc_fprintf_md5(FILE * f, udpc_md5 md5){
  for(int i = 0; i < MD5_DIGEST_LENGTH; i++){
    fprintf(f, "%2X ",  md5.md5[i]);
  }
}

static size_t dirscan_get_item(dirscan * ds, const char * file){
  for(size_t i = 0; i < ds->cnt; i++)
    if(0 == strcmp(file, ds->files[i]))
      return i;
  list_push(ds->files, ds->cnt, iron_clone(file, strlen(file) + 1));
  list_push(ds->md5s, ds->cnt, (udpc_md5){0});
  list_push(ds->last_change, ds->cnt, (time_t){0});
  list_push(ds->size, ds->cnt, (size_t)0);
  list_push(ds->type, ds->cnt, UDPC_DIRSCAN_FILE);
  return ds->cnt++;
}

void udpc_dirscan_update(const char * basedir, dirscan * dir, bool include_directories){
  size_t init_cnt = dir->cnt;
  bool found[dir->cnt];
  memset(found, 0, sizeof(found));
  size_t len = strlen(basedir);
  if(basedir[len - 1] != '/')
    len += 1;
  int ftwf(const char * filename, const struct stat * st, int ft){
    UNUSED(ft);
    bool isdir = S_ISDIR(st->st_mode);
    if(isdir && !include_directories)
      return 0;
    bool isfile = S_ISREG(st->st_mode);
    if(isdir || isfile){

      char * stripped_filename = (char *) filename + len;
      size_t i = dirscan_get_item(dir, stripped_filename);
      if(i < init_cnt)
	found[i] = true;
      size_t old_s = dir->size[i];
      t_us old_t = dir->last_change[i];
      size_t s = st->st_size; 
      t_us t = st->st_mtim.tv_sec * 1000000 + st->st_mtim.tv_nsec / 1000;
      if(s != old_s || old_t != t){
	if(isfile)
	  dir->md5s[i] = udpc_file_md5(filename);
	dir->size[i] = s;
	dir->last_change[i] = t;
	dir->type[i] = isfile ? UDPC_DIRSCAN_FILE : UDPC_DIRSCAN_DIR;
      }
    }
    return 0;
  }
  ftw(basedir, ftwf, 100);
  for(size_t i = init_cnt; i != 0;){
    i -= 1;
    if(false == found[i]){
      // missing files
      dir->type[i] = UDPC_DIRSCAN_DELETED;
    }
  }
}

dirscan scan_directories(const char * basedir){
  dirscan ds = {0};
  udpc_dirscan_update(basedir, &ds, false);
  return ds;
}

void dirscan_print(dirscan ds){
  for(size_t i = 0; i < ds.cnt; i++){
    logd(" %20s |", ds.files[i]); udpc_print_md5(ds.md5s[i]); logd("\n");
  }
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

dirscan_diff udpc_dirscan_diff(dirscan d1, dirscan d2){
  int found_1[d1.cnt];
  int found_2[d2.cnt];
  memset(found_1, 0, sizeof(found_1));
  memset(found_2, 0, sizeof(found_2));
  dirscan_diff diff = {0};

  // slow algorithm.
  for(size_t i = 0; i < d1.cnt; i++){
    for(size_t j = 0; j < d2.cnt; j++){
      if(strcmp(d1.files[i],d2.files[j]) == 0){
	found_1[i] = 1;
	found_2[j] = 1;
	bool changed = false;
	if(d1.type[i] == UDPC_DIRSCAN_DELETED && d2.type[j] == UDPC_DIRSCAN_FILE){
	  list_push(diff.states, diff.cnt, DIRSCAN_GONE_PREV);
	  changed = true;
	}else if(d1.type[i] == UDPC_DIRSCAN_FILE && d2.type[j] == UDPC_DIRSCAN_DELETED){
	  list_push(diff.states, diff.cnt, DIRSCAN_GONE);
	  changed = true;
	}else if(udpc_md5_compare(d1.md5s[i], d2.md5s[j]) == false){
	  list_push(diff.states, diff.cnt, DIRSCAN_DIFF_MD5);
	  changed = true;
	}
	if(changed){
	  list_push(diff.index1, diff.cnt, i);
	  list_push(diff.index2, diff.cnt, j);
	  diff.cnt += 1;
	}
      }
    }
  }
  for(size_t i =0; i < d1.cnt; i++){
    if(found_1[i] == 0){
      list_push(diff.states, diff.cnt, DIRSCAN_NEW);
      list_push(diff.index1, diff.cnt, i);
      list_push(diff.index2, diff.cnt, d2.cnt);
      diff.cnt +=1;
    }
  }
  for(size_t j =0; j < d2.cnt; j++){
    if(found_2[j] == 0){
      list_push(diff.states, diff.cnt, DIRSCAN_NEW);
      list_push(diff.index1, diff.cnt, d1.cnt);
      list_push(diff.index2, diff.cnt, j);
      diff.cnt +=1;
    }
  }
  return diff;
}

void udpc_dirscan_clear_diff(dirscan_diff * diff){
  if(diff->states != NULL){
    dealloc(diff->states);
    dealloc(diff->index1);
    dealloc(diff->index2);
  }
  memset(diff,0,sizeof(*diff));
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
  
  udpc_md5 digest = {0};
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
  dealloc(_dirscan->size);
  dealloc(_dirscan->type);
  memset(_dirscan, 0, sizeof(_dirscan[0]));
}

void * dirscan_to_buffer(dirscan _dirscan, size_t * size){
  void * buffer = NULL;
  udpc_pack_size_t(_dirscan.cnt, &buffer, size);
  for(size_t i = 0; i < _dirscan.cnt; i++)
    udpc_pack_string(_dirscan.files[i], &buffer, size);
  udpc_pack(_dirscan.md5s, _dirscan.cnt * sizeof(_dirscan.md5s[0]), &buffer, size);
  udpc_pack(_dirscan.last_change, _dirscan.cnt * sizeof(_dirscan.last_change[0]), &buffer, size);
  udpc_pack(_dirscan.size, _dirscan.cnt * sizeof(_dirscan.size[0]), &buffer, size);
  udpc_pack(_dirscan.type, _dirscan.cnt * sizeof(_dirscan.type[0]), &buffer, size);
  return buffer;
}

dirscan dirscan_from_buffer(void * buffer){
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
  out.md5s = alloc(sizeof(out.md5s[0]) * cnt);
  udpc_unpack(out.md5s, sizeof(out.md5s[0]) * cnt, &ptr);
  out.last_change = alloc0(sizeof(out.last_change[0]) * cnt);
  udpc_unpack(out.last_change, sizeof(out.last_change[0]) * cnt, &ptr);
  out.size = alloc(sizeof(out.size[0]) * cnt);
  udpc_unpack(out.size, sizeof(out.size[0]) * cnt, &ptr);
  out.type = alloc(sizeof(out.type[0]) * cnt);
  udpc_unpack(out.type, sizeof(out.type[0]) * cnt, &ptr);
  return out;
}

void udpc_dirscan_serve(udpc_connection * con, udpc_connection_stats * stats, dirscan last_dirscan){
  char readbuffer[2000];
  int r = udpc_read(con, readbuffer, sizeof(readbuffer));
  if(r < 8)
    return;
  void * read2 = readbuffer;
  char * code = udpc_unpack_string(&read2);
  if(strcmp(code, udpc_dirscan_service_name) != 0)
    return;
  u64 id = udpc_unpack_size_t(&read2);
  
  size_t send_buf_size = 0;
  void * send_buf = dirscan_to_buffer(last_dirscan, &send_buf_size);
  logd("Sending %i\n", send_buf_size);
  u64 chunk_size = 1400;
  int handle_chunk(const transmission_data * tid, void * chunk,
		   size_t chunk_id, size_t chunk_size, void * userdata){
    UNUSED(tid); UNUSED(userdata);
    logd("Sending chunk %i\n", chunk_id);
    u64 offset = chunk_id * chunk_size;
    u64 size = MIN(send_buf_size - offset, chunk_size);
    memcpy(chunk, send_buf + offset, size);
    return 0;
  }
  int send_status = udpc_send_transmission( con, stats, id, send_buf_size,
			  chunk_size, handle_chunk, NULL);
  //logd("Done with transmission.. %i\n", send_status);
  UNUSED(send_status);
}

int udpc_dirscan_client(udpc_connection * con, udpc_connection_stats * stats, dirscan * dscan){
  u64 service_id = get_rand_u64();
  { // send ids.
    void * ptr = NULL;
    size_t cnt = 0;
    udpc_pack_string(udpc_dirscan_service_name, &ptr, &cnt);
    udpc_pack_size_t(service_id, &ptr, &cnt);    
    udpc_write(con, ptr, cnt);
    dealloc(ptr);
  }
  
  void * buffer = NULL;
  
  int handle_chunk(const transmission_data * tid, const void * chunk,
		   size_t chunk_id, size_t chunk_size, void * userdata){
    UNUSED(userdata);
    if(buffer == NULL){
      buffer = alloc0(tid->total_size);
    }
    memcpy(buffer + chunk_id * tid->chunk_size, chunk, chunk_size);
    return 0;
  }
  int status = udpc_receive_transmission(con, stats, service_id,
					 handle_chunk, NULL);
  if(status < 0)
    return status;
  //logd("Transmission status: %i\n", status);
  *dscan = dirscan_from_buffer(buffer);
  dealloc(buffer);
  return status;
}
