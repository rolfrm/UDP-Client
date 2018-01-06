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
#include <iron/process.h>
#include "udpc.h"
#include "udpc_seq.h"
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
  iron_log_stacktrace();
  raise(SIGINT);
  exit(255);
}

bool should_close = false;
void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}

void sync(){
  iron_process process;
  int proc = iron_process_run("/bin/sync", NULL,  &process);
  logd("PROC: %i\n", process.pid);
  UNUSED(proc);
  ASSERT( IRON_PROCESS_EXITED == iron_process_wait(process, 100000));
}

bool test_dirscan2(){
  system("rm -r dir\\ test\\ 2/");
  dirscan ds = {0};

  mkdir("dir test 2", 0777);
  mkdir("dir test 2/sub dir", 0777);
  udpc_dirscan_update("dir test 2", &ds, false);
  ASSERT(ds.cnt == 0);
  char buffer_test[10] = {100};
  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 2/test1" );
  buffer_test[0] += 10;
  write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 2/test2" );
  iron_usleep(30000);  
  udpc_dirscan_update("dir test 2", &ds, false);
  int idx = -1;
  if(strcmp(ds.files[0], "test1") == 0)
    idx = 0;
  else if(strcmp(ds.files[1], "test1") == 0)
    idx = 1;
  ASSERT(idx != -1);
  ASSERT(ds.type[idx] == UDPC_DIRSCAN_FILE);
  for(size_t i = 0; i < 15; i++){
    remove("dir test 2/test1");
    sync();
    udpc_dirscan_update("dir test 2", &ds, false);
    ASSERT(ds.type[idx] == UDPC_DIRSCAN_DELETED);
    
    write_buffer_to_file(buffer_test, sizeof(buffer_test), "dir test 2/test1" );
    sync();

    udpc_dirscan_update("dir test 2", &ds, false);
    ASSERT(ds.type[idx] == UDPC_DIRSCAN_FILE);
    
    udpc_dirscan_update("dir test 2", &ds, false);
  }
  

  
  ASSERT(ds.cnt == 2);
  
  return TEST_SUCCESS;
}

bool test_dirscan(){
  int buffer_test[10000];
  for(size_t i = 0; i <array_count(buffer_test); i++){
    buffer_test[i] = i;
  }
  allocator * _allocator = trace_allocator_make();
  allocator * old_allocator = iron_get_allocator();
  iron_set_allocator(_allocator);
  system("rm -r dir\\ test\\ 1/");
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
  
  udpc_dirscan_update("dir test 1", &ds, false);
  ASSERT(ds.type[idx] != UDPC_DIRSCAN_DELETED);
  logd("NOT DELETED!\n");
  remove("dir test 1/test1");
  udpc_dirscan_update("dir test 1", &ds, false);
  logd("DELETED!\n");
  ASSERT(ds.type[idx] == UDPC_DIRSCAN_DELETED);

  
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

#include <sys/wait.h>
#include <signal.h>


bool test_udpc_share(){
  const char * arg0[] = {"server", NULL};
  const char *arg1[] = {"share", "test@0.0.0.0:a", "test share", NULL};
  const char * arg2[]= {"share","test@0.0.0.0:a","test share2","test@0.0.0.0:a", NULL};
  char test_code[5000];
  memset(test_code, 'a', sizeof(test_code));
  test_code[sizeof(test_code) - 1] = 0;
  iron_process server_proc, share_1_proc, share_2_proc;
  
  ASSERT(0 == iron_process_run("./server",arg0, &server_proc));  

  // setup dirs
  remove("test share/hello.txt");
  remove("test share/hello2.txt");
  remove("test share/hello3.txt");
  remove("test share");
  remove("test share2/hello.txt");
  remove("test share2/hello2.txt");
  remove("test share2/hello3.txt");
  remove("test share2");

  mkdir("test share/", 0777);
  write_string_to_file(test_code, "test share/hello.txt");
  write_string_to_file(test_code, "test share/hello2.txt");
  mkdir("test share2/", 0777);
  write_string_to_file(test_code, "test share2/hello3.txt");
  iron_usleep(100000);

  ASSERT(0 == iron_process_run("./share", arg1, &share_1_proc));
  iron_usleep(100000);
  ASSERT(0 == iron_process_run("./share", arg2, &share_2_proc));

  logd("Waiting for process %p\n", timestamp());
  logd("STATUS C2: %i\n", iron_process_get_status(share_2_proc));
  iron_process_wait(share_2_proc, 30000000);
  logd("end %p\n", timestamp());
  iron_process_status s_c2 = iron_process_get_status(share_2_proc);
  // Interrupt first then kill in case it did not work.
  kill(server_proc.pid, SIGINT);
  kill(share_1_proc.pid, SIGINT);
  kill(share_2_proc.pid, SIGINT);
  iron_usleep(10000);
  kill(server_proc.pid, SIGINT);
  kill(share_1_proc.pid, SIGINT);
  kill(share_2_proc.pid, SIGINT);
  iron_usleep(10000);
  kill(server_proc.pid, SIGKILL);
  kill(share_1_proc.pid, SIGKILL);
  kill(share_2_proc.pid, SIGKILL);
  iron_usleep(10000);
  iron_process_status s1 = iron_process_get_status(server_proc);
  iron_process_status s_c1 = iron_process_get_status(share_1_proc);
  logd("exit statuses: %i %i %i\n", s1, s_c1, s_c2);
  sync();
  char * file_content = read_file_to_string("test share2/hello.txt");
  char * file_content2 = read_file_to_string("test share2/hello2.txt");
  
  ASSERT(file_content != NULL && file_content2 != NULL);
  logd("l1: %i, l2: %i\n", strlen(test_code), strlen(file_content));
  ASSERT(strcmp(test_code, file_content) == 0);
  ASSERT(strcmp(test_code, file_content2) == 0);
  dealloc(file_content);
  dealloc(file_content2);
  ASSERT(s_c2 == IRON_PROCESS_EXITED);
  iron_usleep(30000); // wait for ports to reopen.
  return TEST_SUCCESS;
}

#include <pthread.h>
bool test_udpc_seq(){
  const char * arg0[] = {"server", NULL};
  iron_process server_proc;
  ASSERT(iron_process_run("./server", arg0, &server_proc) == 0);

  iron_usleep(10000);
  udpc_service * s1 = udpc_login("test@0.0.0.0:a");
  ASSERT(s1 != NULL);
  void * connection_handle(void * info){
    UNUSED(info);
    udpc_connection * c2 = udpc_listen(s1);
    ASSERT( c2 != NULL);
    udpc_set_timeout(c2, 1000000);
    udpc_seq seq1 = udpc_setup_seq_peer(c2);
    char buffer[100];
    u64 seq_num = 0;
    int size = -1;
    for(int i = 0; i < 6 && size == -1; i++)
      size = udpc_seq_read(&seq1, buffer, sizeof(buffer), &seq_num);
    logd("SIZE: %i\n", size);
    ASSERT(size > 0);
    udpc_seq_write(&seq1, buffer, size);
    size = udpc_seq_read(&seq1, buffer, sizeof(buffer), &seq_num);
    ASSERT(size > 0);
    udpc_seq_write(&seq1, buffer, size);
    size = udpc_seq_read(&seq1, buffer, sizeof(buffer), &seq_num);
    ASSERT(size > 0);
    udpc_seq_write(&seq1, buffer, size);    
    udpc_close(c2);
    return NULL;
  }
  pthread_t tid;
  pthread_create( &tid, NULL, connection_handle, NULL);
  iron_usleep(10000);
  udpc_connection * con2 = udpc_connect("test@0.0.0.0:a");
  udpc_set_timeout(con2, 1000000);
  ASSERT(con2 != NULL);
  udpc_seq seq2 = udpc_setup_seq(con2);
  const char * ello = "Ello!\n";
 write_seq1:;
  udpc_seq_write(&seq2, ello, strlen(ello) + 1);
  char buffer[100];
  u64 seq_num2 = 0;
  int r = -1;
  //for(int i = 0; i < 2 && r == -1; i++)
  r = udpc_seq_read(&seq2, buffer, sizeof(buffer), &seq_num2);
  if(r == -1){
    logd("write again..\n");
    goto write_seq1;
  }
  udpc_seq_write(&seq2, ello, strlen(ello) + 1);
  udpc_seq_read(&seq2, buffer, sizeof(buffer), &seq_num2);
  udpc_seq_write(&seq2, ello, strlen(ello) + 1);
  udpc_seq_read(&seq2, buffer, sizeof(buffer), &seq_num2);
  //logd("Buffer: %i: %s", seq_num2, buffer); 
  udpc_close(con2);

  // Interrupt first then kill in case it did not work.
  kill(server_proc.pid, SIGINT);
  iron_usleep(10000);
  kill(server_proc.pid, SIGINT);
  iron_usleep(10000);
  kill(server_proc.pid, SIGKILL);
  pthread_join(tid, NULL);
  return TEST_SUCCESS;
}

bool test_transmission(){
  udpc_connection_stats stats = get_stats();
  const char * srv_arg[] = {"server", NULL};
  iron_process server_proc;
  ASSERT(iron_process_run("./server", srv_arg, &server_proc) == 0);
  iron_usleep(10000);
  udpc_service * s1 = udpc_login("test@0.0.0.0:a");
  ASSERT(s1 != NULL);
  u64 service_id = 12345;
  int sent = 0;
  int received = 0;
  bool ok = true;
  void * connection_handle(void * info){
    UNUSED(info);
    udpc_connection * c2 = udpc_listen(s1);

    int handle_chunk(const transmission_data * tid, const void * _chunk,
		     size_t chunk_id, size_t chunk_size, void * userdata){
      received += 1;
      UNUSED(userdata); UNUSED(tid);
      const int * chunk = _chunk;
      int chunk_cnt = chunk_size / sizeof(int);
      for(int i = 0; i < chunk_cnt; i++){
	ok &= (chunk[i] == (int)((chunk_id * (tid->chunk_size/4)) + i));

      }
      return 0;
    }
    int status = udpc_receive_transmission(c2, &stats, service_id,
					   handle_chunk, NULL);
    udpc_close(c2);
    UNUSED(status);
    //ASSERT(status == 0);
    return NULL;
  }
  pthread_t tid;
  pthread_create( &tid, NULL, connection_handle, NULL);
  iron_usleep(10000);
  udpc_connection * con = udpc_connect("test@0.0.0.0:a");
  
  int handle_chunk(const transmission_data * tid, void * _chunk,
		   size_t chunk_id, size_t chunk_size, void * userdata){
    UNUSED(tid); UNUSED(userdata);
    int * chunk = _chunk;
    for(size_t i = 0; i < chunk_size /4; i++){
      chunk[i] = chunk_id * (tid->chunk_size/4) + (int)i;
    }
    sent += 1;
    return 0;
  }
  udpc_send_transmission( con, &stats, service_id, 134561,
			  1400, handle_chunk, NULL);
  udpc_close(con);
  kill(server_proc.pid, SIGINT);
  iron_usleep(10000); 
  kill(server_proc.pid, SIGINT);
  iron_usleep(10000);
  kill(server_proc.pid, SIGKILL);
  pthread_join(tid, NULL);
  logd("OK? %s\n", ok ? "OK" : "NO");
  logd("TX/RX: %i/%i\n", sent, received);
  ASSERT(ok);
  ASSERT(sent > 0);
  ASSERT(received > 0);
  ASSERT(sent >= received);

  return TEST_SUCCESS;
}

int test_basic_functionality(){
  void * server_thread( void * unused){
    UNUSED(unused);
    udpc_start_server("0.0.0.0");
    return NULL;
  }

  iron_thread * trd = iron_start_thread(server_thread, NULL);
  UNUSED(trd);
  iron_sleep(0.25);
  for(int i = 0; i< 100000; i++){
    logd("TEST BASIC: %i\n", i);

    udpc_service * s1 = udpc_login("test@0.0.0.0:a");
    while(s1 == NULL)
      s1 = udpc_login("test@0.0.0.0:a");
    iron_sleep(0.1);
    udpc_connection * s3 = NULL;
    void * send_test_code(void * unused){
      UNUSED(unused);
      while(s3 == NULL)
	s3 = udpc_listen(s1);
      
      const char * buf = "test test";
      udpc_write(s3, buf, strlen(buf) + 1);
      return NULL;
    }
    
    iron_thread * trd2= iron_start_thread(send_test_code, NULL);
    
    udpc_connection * s2 = udpc_connect("test@0.0.0.0:a");
    while(s2 == NULL)
      s2 = udpc_connect("test@0.0.0.0:a");
    //iron_sleep(0.01);
    
    char buffer2[20];
    udpc_read(s2, buffer2,sizeof(buffer2));
    ASSERT(strcmp(buffer2, "test test") == 0);
    iron_thread_join(trd2);
    udpc_close(s3);
    udpc_close(s2);
    udpc_logout(s1);
  }
  
  return TEST_SUCCESS;
}

int main(){
  TEST(test_basic_functionality);
  //TEST(test_dirscan);
  //TEST(test_dirscan2);
  //TEST(test_udpc_seq);
  //TEST(test_transmission);
  //TEST(test_udpc_share);
  
  return 0;
}
