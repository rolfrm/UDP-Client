#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <unistd.h> //chdir
#include <time.h> //difftime


#include <stdint.h>
#include <iron/types.h>
#include <iron/log.h>
#include <iron/utils.h>
#include <iron/time.h>

#include "udpc.h"
#include "udpc_seq.h"
#include "udpc_utils.h"
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

void ensure_directory(const char * path);

int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);

  char * dir = argv[2];
  ensure_directory(dir);
  struct stat dirst;
  stat(dir, &dirst);
  ASSERT(S_ISDIR(dirst.st_mode));

  if(argc == 3){
    dirscan scan_result = {0};
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);

    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);   
      if(c2 == NULL)
	continue;
      udpc_set_timeout(c2, 10000000);
      udpc_connection_stats stats = get_stats();
      while(true){
	char buf[1024];
	int r = udpc_peek(c2, buf, sizeof(buf));
	if(r == -1)
	  break;
	logd("Buffer: %s\n", buf);
	void * rcv_str = buf;
	char * st = udpc_unpack_string(&rcv_str);
	if(strcmp(st, udpc_file_serve_service_name) == 0){
	  logd("File share\n");
	  udpc_file_serve(c2, &stats, dir);
	  logd("File share END\n");
	}else if(strcmp(st, udpc_speed_test_service_name) == 0){
	  logd("Speed \n");
	  udpc_speed_serve(c2, NULL);
	  logd("Speed END \n");
	}else if(strcmp(st, udpc_dirscan_service_name) == 0){
	  logd("Sending dir..\n");
	  udpc_dirscan_update(dir, &scan_result,false);
	  udpc_dirscan_serve(c2, &stats, scan_result);
	  logd("Sending dir END\n");
	}else{
	  loge("Unknown service '%s'\n", st);
	  break;
	}
      }
      logd("END!\n");
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc == 4){
    char * servicename = argv[1];

    UNUSED(servicename);
    char * other_service = argv[3];
    int delay = 200;
    int bufsize = 1400;
    udpc_connection * con = udpc_connect(other_service);
    // find speed/packagesize
    int test_delay = delay;
    double rtt = 100;
    for(int i = 0; i < 10; i++){
      logd("%i Testing connection: %i\n", i, test_delay);
      int missed = 0, missed_seqs = 0;
      double mean_rtt, peak_rtt;
      udpc_speed_client(con, test_delay, bufsize, 100, &missed, &missed_seqs, &mean_rtt, &peak_rtt);
      logd("Connection Check:\ndelay: %i\nMissed: %i\nMean RTT: %f s\nPeak RTT:%f s\n", test_delay, missed, mean_rtt, peak_rtt);
      
      if(mean_rtt < rtt){
	delay = test_delay;
	test_delay = (test_delay * 2) / 3 ;
      }
      else{
	test_delay = (3 * test_delay) / 2;
      }
      rtt = mean_rtt;
    }

    dirscan local_dir = scan_directories(dir);
    bool local_found[local_dir.cnt];
    memset(local_found,0, sizeof(local_found));
  do_dirscan:;
    dirscan ext_dir = {0};
    udpc_connection_stats stats = get_stats();
    int ok = udpc_dirscan_client(con, &stats, &ext_dir);
    if(ok < 0) goto do_dirscan;

    logd ("got dirscan\n");
    dirscan_diff diff = udpc_dirscan_diff(local_dir, ext_dir);
    for(size_t i = 0; i < diff.cnt; i++){
      size_t i1 = diff.index1[i];
      size_t i2 = diff.index2[i];
      dirscan_state state = diff.states[i];
      double difft = 1;
      switch(state){
      case DIRSCAN_GONE:
	{
	  char * f = ext_dir.files[i1];
	  logd("Gone: %s\n");
	  char filepathbuffer[1000];
	  sprintf(filepathbuffer, "%s/%s",dir, f);

	  remove(filepathbuffer);
	}
	break;
      case DIRSCAN_DIFF_MD5:
	difft = difftime(ext_dir.last_change[i2], local_dir.last_change[i1]);
      case DIRSCAN_NEW:
	{
	  delay = 10;
	  char * f = ext_dir.files[i2];
	  logd("changed/new: %s\n");
	  char filepathbuffer[1000];
	  sprintf(filepathbuffer, "%s/%s",dir, f);
	  logd("FIle: %s\n", filepathbuffer);
	  if( difft >= 0){
	    logd("Send to local\n");
	    udpc_file_client(con, &stats, ext_dir.files[i2], filepathbuffer);
	  }else if (difft < 0){
	    logd("Send to remote\n");
	    udpc_file_client2(con, &stats, ext_dir.files[i2], filepathbuffer);
	  }
	  
	}
	break;
      }
    }

    
    udpc_write(con, "asd", sizeof("asd"));

    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
