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
#include <iron/test.h>
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

bool test_dirscan(){
  int buffer_test[10000];
  for(size_t i = 0; i <array_count(buffer_test); i++){
    buffer_test[i] = i;
  }
  allocator * _allocator = trace_allocator_make();
  allocator * old_allocator = iron_get_allocator();
  iron_set_allocator(_allocator);
  
  dirscan ds = {0};
  //size_t max_diff_cnt = 0;
  //size_t max_file_cnt = 0;
  mkdir("dir test 1", 0777);
  mkdir("dir test 1/sub dir", 0777);
  memset(buffer_test, 10, sizeof(buffer_test));
  udpc_dirscan_update("dir test 1", &ds, false);
  ASSERT(ds.cnt == 0);
  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/test1" );
  buffer_test[0] += 10;
  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/test2" );
  iron_usleep(30000);  
  size_t s = 0;
  void * buffer = dirscan_to_buffer(ds, &s);
  dirscan copy = dirscan_from_buffer(buffer);
  dealloc(buffer);
  udpc_dirscan_update("dir test 1", &ds, false);

  dirscan_diff diff = udpc_dirscan_diff(copy, ds);
  ASSERT(diff.cnt == 2);
  ASSERT(diff.states[0] == DIRSCAN_NEW);
  udpc_dirscan_clear_diff(&diff);
  ASSERT(ds.type[0] == UDPC_DIRSCAN_FILE);
  remove("dir test 1/test1");
  iron_usleep(20000);
  udpc_dirscan_update("dir test 1", &ds, false);
  
  int idx = -1;
  if(strcmp(ds.files[0], "test1") == 0)
    idx = 0;
  else if(strcmp(ds.files[1], "test1") == 0)
    idx = 1;

  ASSERT(idx != -1);
  iron_usleep(30000);
  ASSERT(ds.cnt == 2);
  ASSERT(ds.type[idx] == UDPC_DIRSCAN_DELETED);

  buffer_test[0] += 10;
  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/test1" );
  udpc_dirscan_update("dir test 1", &copy, false);
  diff = udpc_dirscan_diff(ds, copy);
  ASSERT(diff.cnt == 1);
  ASSERT(diff.states[0] == DIRSCAN_NEW);
  udpc_dirscan_clear_diff(&diff);
  
  dirscan_clean(&copy);

  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 1/sub dir/test3" );
  udpc_dirscan_update("dir test 1", &ds, false);
  ASSERT(ds.cnt == 3);
  ASSERT(udpc_md5_compare(ds.md5s[idx], ds.md5s[2]));

  for(int i = 0; i < 10; i++){
    char filename[40];
    sprintf(filename, "dir test 1/sub dir/testx_%i", i);
    write_buffer_to_file(buffer_test, sizeof(buffer_test), filename );
  }
  udpc_dirscan_update("dir test 1", &ds, false);
  ASSERT(ds.cnt == 13);
 
  dirscan_clean(&ds);

  for(int i = 0; i < 10; i++){
    char filename[40];
    sprintf(filename, "dir test 1/sub dir/testx_%i", i);
    remove(filename);
  }
  
  ASSERT(!remove("dir test 1/test2"));
  ASSERT(!remove("dir test 1/sub dir/test3"));
  ASSERT(!remove("dir test 1/sub dir"));
  ASSERT(!remove("dir test 1/test1"));
  ASSERT(!remove("dir test 1"));
  iron_set_allocator(old_allocator);
  int used_pointers = (int) trace_allocator_allocated_pointers(_allocator);
  trace_allocator_release(_allocator);
  
  ASSERT(used_pointers == 0);
  return TEST_SUCCESS;
}

int main(){
  TEST(test_dirscan);
  return 0;
}
