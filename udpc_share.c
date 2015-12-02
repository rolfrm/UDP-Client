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

void test_stuff(char ** argv){
  struct stat st;
  stat(argv[1], &st);
  
  if( S_ISREG(st.st_mode) ) {
    // file exists
    udpc_md5 md5 = udpc_file_md5(argv[1]);
    logd("MD5: ");
    udpc_print_md5(md5);
    logd("\n");
  } else if( S_ISDIR(st.st_mode)){
    // file doesn't exist
    dirscan dsc = scan_directories(argv[1]);
    logd("Found: %i files\n",dsc.cnt);
    dirscan_clean(&dsc);
  }
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
    char * servicename = argv[1];
    udpc_service * con = udpc_login(servicename);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      while(true){
	size_t r = 0;
	char buf[1024];
	while(r == 0)
	  r = udpc_read(c2, buf, sizeof(buf));
	void * rcv_str = buf;
	char * st = udpc_unpack_string(&rcv_str);
	if(strcmp(st, udpc_file_serve_service_name) == 0){
	  char * cdir = get_current_dir_name();
	  chdir(dir);
	  udpc_file_serve(c2, buf);
	  chdir(cdir);
	}else if(strcmp(st, udpc_speed_test_service_name) == 0){
	  udpc_speed_serve(c2, buf);
	}else if(strcmp(st, udpc_dirscan_service_name) == 0){
	  logd("Dirscan!\n");
	  dirscan scan_result  = scan_directories(dir);
	  udpc_dirscan_serve(c2, scan_result, 1000, 1400, buf);
	  dirscan_clean(&scan_result);
	}else{
	  loge("Unknown service '%s'\n", st);
	  break;
	}
      }
	
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc == 4){
    char * servicename = argv[1];

    UNUSED(servicename);
    char * other_service = argv[3];
    int delay = 40;
    int bufsize = 1000;
    udpc_connection * con = udpc_connect(other_service);
    // find speed/packagesize
    int test_delay = delay;
    for(int i = 0; i < 10; i++){
      logd("%i Testing connection: %i\n", i, test_delay);
      int missed = 0, missed_seqs = 0;
      udpc_speed_client(con, test_delay, bufsize, 1000, &missed, &missed_seqs);
      logd("Missed: %i\n", missed);
      if(missed == 0){
	delay = test_delay;
	test_delay = test_delay / 2;
      }
      else{
	test_delay = 2 * test_delay;
      }
      
    }

    dirscan local_dir = scan_directories(dir);
    bool local_found[local_dir.cnt];
    memset(local_found,0, sizeof(local_found));
    dirscan ext_dir;
    int ok = udpc_dirscan_client(con, &ext_dir);
    int match[ext_dir.cnt];
    if(ok != -1){
      for(size_t i = 0; i < ext_dir.cnt; i++){
	match[i] = -1;
	char * ext_file = ext_dir.files[i];
	logd("ext file: %s\n", ext_file);
	for(size_t j = 0; j < local_dir.cnt; j++){
	  char * loc_file = local_dir.files[j];
	  if(strcmp(ext_file, loc_file) == 0){
	    local_found[j] = true;
	    match[i] = j;
	    break;
	  }
	}
      }
      delay = 0;
      char * cdir = get_current_dir_name();
      chdir(dir);
      for(size_t i = 0; i < ext_dir.cnt; i++){
	logd("match: %i\n", match[i]);
	if(match[i] == -1 || false == udpc_md5_compare(ext_dir.md5s[i], local_dir.md5s[match[i]])){
	  double difft = (match[i] == -1) ? 1 : (difftime(ext_dir.last_change[i], local_dir.last_change[match[i]]));
	  logd("Transferring: '%s'\n", ext_dir.files[i]);
	  ensure_directory(ext_dir.files[i]);
	  if( difft > 0){
	    logd("Send to local\n");
	    udpc_file_client(con, delay, 1400, ext_dir.files[i], ext_dir.files[i]);
	  }else if (difft < 0){
	    logd("Send to remote\n");
	    udpc_file_client2(con, delay, 1400, ext_dir.files[i], ext_dir.files[i]);
	  }
	}
      }
      for(size_t i = 0; i < local_dir.cnt; i++){
	if(local_found[i] == false){
	  logd("Sending file back: '%s'\n", local_dir.files[i]);
	  udpc_file_client2(con, delay, 1400, local_dir.files[i], local_dir.files[i]);
	}
	logd("Found? %i '%s'\n", local_found[i], local_dir.files[i]);
      }
      chdir(cdir);
    }
    
    udpc_write(con, "asd", sizeof("asd"));

    udpc_close(con);
    
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
