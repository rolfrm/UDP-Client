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
#include <iron/fileio.h>
#include <iron/process.h>
#include "udpc.h"
#include "udpc_seq.h"
#include "udpc_utils.h"
#include "udpc_send_file.h"
#include "udpc_stream_check.h"
#include "udpc_dir_scan.h"
#include "udpc_share_log.h"
#include "udpc_share_delete.h"

void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  iron_log_stacktrace();
  //raise(SIGSTOP);
  exit(10);
}

bool should_close = false;

void handle_sigint(int signum){
  logd("Caught sigint %i\n", signum);
  iron_log_stacktrace();
  should_close = true;
  signal(SIGINT, NULL); // next time just quit.
}

void ensure_directory(const char * path);

void handle_arg(int argc, char ** argv, const char * pattern, int args, void * handler, void * userdata)
{
  int index = -1;
  for(int i = 0; i < argc; i++){
    if(strcmp(argv[i], pattern) == 0){
      index = i;
      break;
    }
  }
  if(index == -1)
    return;
  int rest_args = argc - index - 1;
  ASSERT(rest_args >= args);
  void (* f)(void * userdata, ...) = handler;
  char ** arg_offset = argv + index + 1;
  switch(args){
  case 0: f(userdata);return;
  case 1: f(userdata, arg_offset[0]);return;
  case 2: f(userdata, arg_offset[0], arg_offset[1]);return;
  default: ERROR("Not supported number of command line args for %s: %i", pattern, args);
  }
}

void handle_data_log(void * userdata, char * log_file){
  UNUSED(userdata);
  logd("Setting log file to %s\n", log_file);
  share_log_set_file(log_file);  
}

dirscan dirscan_clone(dirscan ds){
  size_t s = 0;
  void * ds_buf = dirscan_to_buffer(ds, &s);
  ASSERT(s > 0);

  dirscan new = dirscan_from_buffer(ds_buf);
  free(ds_buf);
  return new;
}

int main(int argc, char ** argv){
  bool persist = false;
  void handle_persist(void * userdata){
    UNUSED(userdata);
    persist = true;
  }
  handle_arg(argc, argv, "--data-log", 1, handle_data_log, NULL);
  handle_arg(argc, argv, "--persist", 0, handle_persist, NULL);

  signal(SIGINT, handle_sigint);

  char * dir = argv[2];
  ensure_directory(dir);
  struct stat dirst;
  stat(dir, &dirst);
  if(!S_ISDIR(dirst.st_mode)){
    ERROR("Directory '%s' does not exist!", dir);
  }

  if(argc == 3){
    dirscan scan_result = {0};
    dirscan backbuf = {0};
    iron_mutex m = iron_mutex_create();
    //udpc_dirscan_update(dir, &scan_result,false);
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);
    void * do_update(void * userdata){
      UNUSED(userdata);
      while(true){
	logd("Update..\n");
	iron_mutex_lock(m);
	udpc_dirscan_update(dir, &backbuf, false);
	dirscan_diff diff = udpc_dirscan_diff(scan_result, backbuf);
	iron_mutex_unlock(m);
	logd("dir diff: %i\n", diff.cnt);
	if(diff.cnt > 0){
	  for(size_t i = 0; i < diff.cnt; i++){
	    logd(diff.states[i] == DIRSCAN_GONE ? "Deleted " : "changed ");
	    logd("%i \n", diff.index1[i], diff.index2[i]);

	  }
	  SWAP(scan_result, backbuf);
	  logd ("new dirscan:\n");
	  
	  for(size_t i = 0; i < scan_result.cnt;i++){
	    logd("Local: %s\n", scan_result.files[i]);
	  }
	}
	udpc_dirscan_clear_diff(&diff);
	iron_usleep(1000000);
      }
      
      return NULL;
    }
    iron_start_thread(do_update, NULL);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);   
      if(c2 == NULL)
	continue;
      udpc_set_timeout(c2, 10000000);
      udpc_connection_stats stats = get_stats();
      while(true){
	char buf[1024];
	udpc_set_timeout(c2, 10000000);
	int r = udpc_peek(c2, buf, sizeof(buf));
	
	if(r == -1){
	  ERROR("Communication lost..\n");
	  break;
	}
	
	void * rcv_str = buf;
	char * st = udpc_unpack_string(&rcv_str);
	if(strcmp(st, udpc_file_serve_service_name) == 0){
	  logd("File share\n");
	  udpc_file_serve(c2, &stats, dir);
	}else if(strcmp(st, udpc_speed_test_service_name) == 0){
	  logd("Speed \n");
	  udpc_speed_serve(c2, NULL);
	}else if(strcmp(st, udpc_dirscan_service_name) == 0){
	  logd("%u SERVE DIR..\n", timestamp());
	  iron_mutex_lock(m);
	  udpc_dirscan_serve(c2, &stats, scan_result);
	  iron_mutex_unlock(m);
	}else if(udpc_delete_serve(c2, dir)){

	}else{
	  ERROR("Unknown service '%s'\n", st);
	  break;
	}
      }
      logd("END!!\n");
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc >= 4){
    char * servicename = argv[1];
    UNUSED(servicename);
    char * other_service = argv[3];
    udpc_connection * con = udpc_connect(other_service);
    if(con == NULL){
      ERROR("Could not connect to '%s'\n", other_service);
      return 1;
    }
    dirscan local_dir = {0};
    dirscan backbuf = {0};
    iron_mutex m = iron_mutex_create();
    // this dirscan model does not work
    // the client keeps pinging, so a bunch of packets
    // queues up. dirscan needs to happen in an async
    // way, so that we can pretend that no changes has
    // happened until the MD5s has been calculated.

    // one communication thread for each client

    void run_update(){
      logd("Update..\n");
      iron_mutex_lock(m);
      udpc_dirscan_update(dir, &backbuf, false);
      
      dirscan_diff diff = udpc_dirscan_diff(local_dir, backbuf);
	
      if(diff.cnt > 0){
	SWAP(local_dir, backbuf);
	dirscan_clean(&backbuf);
	backbuf = dirscan_clone(local_dir);
      }
      iron_mutex_unlock(m);
      udpc_dirscan_clear_diff(&diff);
    }
    
    void * do_update(void * userdata){
      UNUSED(userdata);
      while(true){
	run_update();
	iron_usleep(1000000);
      }
      
      return NULL;
    }
    UNUSED(do_update);
    while(true){

      // find speed/packagesize
      /*int test_delay = delay;
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
	}*/

      bool local_found[local_dir.cnt];
      memset(local_found,0, sizeof(local_found));
    do_dirscan:;
      dirscan ext_dir = {0};
      udpc_connection_stats stats = get_stats();
      logd("Req dirscan...\n");
     int ok = udpc_dirscan_client(con, &stats, &ext_dir);
     if(ok < 0) {
       iron_usleep(100000);
       int timeout = udpc_get_timeout(con);
       logd("set timeout.. %i\n", timeout);
       udpc_set_timeout(con, 0);

       for(size_t i = 0; i < 5; i++)
	 if(-1 == udpc_read(con, NULL, 0))
	   break;
       udpc_set_timeout(con, timeout);
       goto do_dirscan;

     }
      run_update(NULL);
      
      logd ("EXT DIR\n");
      udpc_dirscan_print(ext_dir);
      logd("LOCAL DIR\n");
      udpc_dirscan_print(local_dir);
      for(size_t i = 0; i < ext_dir.cnt;i++){
	logd("EXT: %s\n", ext_dir.files[i]);
      }
      for(size_t i = 0; i < local_dir.cnt;i++){
	logd("Local: %s\n", local_dir.files[i]);
      }
      iron_mutex_lock(m);
      dirscan_diff diff = udpc_dirscan_diff(local_dir, ext_dir);
      iron_mutex_unlock(m);
      for(size_t i = 0; i < diff.cnt; i++){
	size_t i1 = diff.index1[i];
	size_t i2 = diff.index2[i];
	dirscan_state state = diff.states[i];
	double difft = i2 == ext_dir.cnt ? -1 : 1;
	switch(state){
	case DIRSCAN_GONE:
	  {
	    char * f = local_dir.files[i1];
	    logd("Gone remote: %s\n", f);
	    char filepathbuffer[1000];
	    sprintf(filepathbuffer, "%s/%s",dir, f);
	    remove(filepathbuffer);
	    share_log_file_deleted(filepathbuffer);
	    logd("Deleted remote\n");
	  }
	  break;
	case DIRSCAN_GONE_PREV:
	  {
	    char * f = ext_dir.files[i2];
	    logd("Gone local: %s\n", f);
	    udpc_delete_client(con, ext_dir.files[i2]);
	    logd("Deleted local\n");
	    break;
	  }
	case DIRSCAN_DIFF_MD5:
	  difft = difftime(ext_dir.last_change[i2], local_dir.last_change[i1]);
	  // -fallthrough
	  //__attribute__((fallthrough));
	case DIRSCAN_NEW:
	  {
	    char * f = NULL;
	    if(i2 != ext_dir.cnt)
	      f = ext_dir.files[i2];
	    else if(i1 != local_dir.cnt)
	      f = local_dir.files[i1];
	    else ASSERT(false);
	    char filepathbuffer[1000];
	    sprintf(filepathbuffer, "%s/%s",dir, f);
	    logd("FIle: %s\n", filepathbuffer);
	    if( difft >= 0){
	      logd("Send to local\n");
	      udpc_file_client(con, &stats, f, filepathbuffer);
	      logd("send to local Done\n");
	    }else if (difft < 0){
	      logd("Send to remote\n");
	      udpc_file_client2(con, &stats, f, filepathbuffer);
	      logd("send to remote Done\n");
	    }
	  
	  }
	  break;
	}
      }
      if(!persist || should_close){
	break;
      }else{
	iron_usleep(1000000);
      }
    }

    udpc_write(con, "end", sizeof("end"));
    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
