#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h> //chdir
#include <time.h> //difftime

#include "udpc.h"
#include "udpc_utils.h"
#include <stdint.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/utils.h>
#include <iron/time.h>
#include "udpc_send_file.h"
#include "udpc_stream_check.h"
#include "udpc_dir_scan.h"

void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGSTOP);
  exit(255);
}

bool should_close = false;

void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}

typedef struct{
  int sequence_id;
  udpc_connection * con;
  
  void * send_buffer;
  int * snd_idx;
  int * snd_size;
  int send_idx_cnt;
  int send_index;
  int total_send_size;
  
}rudpc_con;

rudpc_con * rudpc_start(udpc_connection * con){
  static int nseq = 0;
  rudpc_con * ncon = alloc0(sizeof(rudpc_con));
  ncon->sequence_id = nseq++;
  return ncon;
}

void rudpc_update(rudpc_con * con){
  
}

void rudpc_write(rudpc_con * con, void * buffer, size_t size){
  con->send_buffer = ralloc(con->send_buffer, size + con->total_send_size);
  int this_start = con->total_send_size;
  int idx = con->send_index++;
  con->total_send_size += size;
  
}

int rudpc_read(rudpc_con * con, void * buffer, int max_size){

}

void rudpc_stop(rudpc_con * con){

}

void ensure_directory(const char * path);

int main(int argc, char ** argv){
  int start_seq = 1010;
  int get_seq = 1011;
  int end_seq = 1012;
  srand(time(NULL));
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);
    while(!should_close){
      logd("Retry\n");
      udpc_connection * c2 = udpc_listen(con);
      if(c2 == NULL)
	continue;
      udpc_set_timeout(c2, 100000);
      int current_seq = -1;
      while(true){
	void * sndbuf = NULL;
	size_t sndbuf_size = 0;
	  
	char buf[1024];
	int r = udpc_read(c2, buf, sizeof(buf));
	if(r < 0)
	  continue;
	void *ptr = buf;
	int cmd = unpack_int(&ptr);
	if(cmd == start_seq){
	  current_seq = unpack_int(&ptr);
	}else if(cmd == get_seq){
	  udpc_pack_int(current_seq, &sndbuf, &sndbuf_size);
	  udpc_write(c2, sndbuf, sndbuf_size);
	  sndbuf_size = 0;
	}else if(cmd == end_seq){
	  current_seq = -1;
	}
      }
      //break;
      /*
      udpc_set_timeout(c2, 100000);
      dirscan scan_result = scan_directories("testdir");
      udpc_dirscan_serve(c2, scan_result, 1000, 1400, NULL);
      dirscan_clean(&scan_result);
      */
      udpc_close(c2);
    }
    logd("Logging out..\n");
    udpc_logout(con);
  }else if(argc == 3){
    char * servicename = argv[1];
  getcon:;
    udpc_connection * con = udpc_connect(servicename);
    logd("logging in\n");
    if(con == NULL)
      goto getcon;
    
    udpc_set_timeout(con, 100000);
    int seq_id = 1234;
    void * buffer = NULL;
    size_t buf_size = 0;
    pack_int(start_seq,&buffer, &buf_size);
    pack_int(seq_id, &buffer, &buf_size);
  establish_seq:
    udpc_write(con, buffer, buf_size);
    buf_size = 0;
    pack_int(get_seq, &buffer, &buf_size);
    udpc_write(con, buffer, buf_size);
    char r_buf[10];
    int r = udpc_read(con, r_buf, sizeof(r_buf));
    if(r < 0)
      goto establish_seq;
      
          /*logd("logged in\n");
    //  do_dirscan:;
    
    dirscan ext_dir;

    int ok = udpc_dirscan_client(con, &ext_dir);
    logd("Done with scan..\n");
    if(ok == -1) {
      udpc_close(con);
      goto getcon;
    }
    dirscan_print(ext_dir);
    logd("Got dir!\n");/*
    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
