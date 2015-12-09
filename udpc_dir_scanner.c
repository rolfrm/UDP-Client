// simple library to userspace UDPC lib to test connection speed.
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

#include <iron/types.h>
#include <iron/log.h>
#include <iron/mem.h>
#include <iron/time.h>

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
  raise(SIGSTOP);
  exit(255);
}

bool should_close = false;
void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}


// UDPC Speed test
// usage:
// SERVER: udpc_get name@server:service
// CLIENT: udpc_get name@server:service delay [buffer_size] [package-count]
int main(int argc, char ** argv){

  if(1 < argc && 0 == strcmp(argv[1], "--test")){
    
    //void udpc_dirscan_update(const char * basedir, dirscan * dir)
    dirscan ds = {0};
    for(int i = 0; i < 100; i++){
      udpc_dirscan_update("testdir1", &ds);
      for(size_t i = 0; i < ds.cnt; i++){
	logd("%s ", ds.files[i]);
	udpc_print_md5(ds.md5s[i]);
	logd("\n");
      }
      ASSERT(ds.cnt > 0);
      iron_usleep(1000000);
    }
    return 0;
  }

     
  
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      udpc_speed_serve(c2, NULL);
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc > 2){

    int delay = 10;
    int bufsize = 1500;
    int count = 10000;
    sscanf(argv[2],"%i", &delay);
    if(argc > 3)
      sscanf(argv[3],"%i", &bufsize);
    if(argc > 4)
      sscanf(argv[4],"%i", &count);
    logd("Delay: %i, buffer size: %i count: %i\n", delay, bufsize, count);
    udpc_connection * con = udpc_connect(argv[1]);
    if(con != NULL){
      int missed = 0, missed_seqs = 0;
      u64 ts1 = timestamp();
      udpc_speed_client(con, delay, bufsize, count, &missed, &missed_seqs);
      i64 successfull = (count - missed);
      logd("Missed: %i seqs: %i\n", missed, missed_seqs);
      logd("Sent: %llu MB\n",((i64)bufsize * successfull) / 1000000);
      u64 ts2 = timestamp();
      double dt = 1e-6 * (double)(ts2 - ts1);
      logd("in %f s\n", dt);
      logd("Speed: %i MB/s\n", (int)(((i64)bufsize * successfull) / dt/ 1e6) );
      udpc_close(con);
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
