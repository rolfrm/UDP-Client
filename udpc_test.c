// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/mem.h>
#include <iron/time.h>
#include <iron/fileio.h>
#include <iron/utils.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_dir_scan.h"
#include "udpc_stream_check.h"

void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGINT);
  exit(255);
}

bool should_close = false;
void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}
void test_buffer_bug();

// UDPC Speed test
// usage:
// SERVER: udpc_get name@server:service
// CLIENT: udpc_get name@server:service delay [buffer_size] [package-count]
int main(){
  //test_buffer_bug();
  //return 0;
  int buffer_test[1000];
  for(size_t i = 0; i <array_count(buffer_test); i++){
    buffer_test[i] = i;
  }
  allocator * _allocator = trace_allocator_make();
  allocator * old_allocator = iron_get_allocator();
  iron_set_allocator(_allocator);
  
  dirscan ds = {0};
  size_t max_diff_cnt = 0;
  size_t max_file_cnt = 0;
  mkdir("dir test 1", 0777);
  mkdir("dir test 1/sub dir", 0777);
  for(int i = 0; i < 10; i++){
    memset(buffer_test, i, sizeof(buffer_test));
    size_t s = 0;
    void * buffer = dirscan_to_buffer(ds, &s);
    dirscan copy = dirscan_from_buffer(buffer);
    dealloc(buffer);
    buffer = NULL;
    ASSERT(ds.cnt == copy.cnt);

    //sync();
    udpc_dirscan_update("dir test 1", &ds, false);

    dirscan_diff diff = udpc_dirscan_diff(copy, ds);
    if(i > 0)
      ASSERT(diff.cnt > 0);
    max_diff_cnt = MAX(diff.cnt, max_diff_cnt);
    max_file_cnt = MAX(ds.cnt, max_file_cnt);
    logd("%i cnt: %i diff cnt: %i\n", i, ds.cnt, diff.cnt);
    udpc_dirscan_clear_diff(&diff);
    dirscan_clean(&copy);
    for(size_t i = 0; i < ds.cnt; i++){
      logd("%s \t", ds.files[i]);
      if(ds.type[i] == UDPC_DIRSCAN_FILE)
	udpc_print_md5(ds.md5s[i]);
      logd("\n");
      }
    //iron_usleep(100000);
  
    write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/test1" );    
    buffer_test[0] += 10;
    write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/test2" );
    buffer_test[0] += 10;
    if((i % 10) == 5)
      write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/sub dir/test3" );
    size_t __s;
    iron_usleep(10000);
    //sync();
    int * __buffer = read_file_to_buffer("dir test 1/test1", &__s);
    logd("AB: %i %i %i\n",__buffer[0], buffer_test[0] - 20, __buffer[0] == (buffer_test[0] - 20));
    ASSERT(__buffer[0] == (buffer_test[0] - 20));
    dealloc(__buffer);
    
  }
  dirscan_clean(&ds);
  remove("dir test 1/test2");
  remove("dir test 1/sub dir/test3");
  remove("dir test 1/sub dir");
  remove("dir test 1/test1");
  int ok = remove("dir test 1");
  logd("Unlink: %i\n", ok);
  iron_set_allocator(old_allocator);
  int used_pointers = (int) trace_allocator_allocated_pointers(_allocator);
  logd("unfreed pointers: %i\n", used_pointers);
  trace_allocator_release(_allocator);
  
  ASSERT(used_pointers == 0);
  ASSERT(max_file_cnt == 3);
  ASSERT(max_diff_cnt == 3);
  return 0;
  
}
