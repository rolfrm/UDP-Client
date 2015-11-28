//
// UDPC File Share
// 
// This program implements file sharing/backup across UDPC.
// The idea is that a number of peers agrees to share a folder.
// The peers will try to guarantee that the most recent version
// of a file is in all the share folders.
//
// The folders will be synced through the help of a few sharing protocols.
// 1. There needs to be some kind of conflict handling implemented
// 2. The sync mechanism should work like rsync, where hashing can be used to figure out which chunks of the file are the same. Initially it will just calculate a md5 hash of the files.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "udpc.h"
#include "udpc_utils.h"
#include "service_descriptor.h"
#include <iron/log.h>

#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include "udpc_send_file.h"

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


#include <openssl/md5.h>

void file_md5(const char * path){
  FILE * f = fopen(path, "r");
  ASSERT(f != NULL);
  unsigned long r = 0;
  char buffer[1024];
  MD5_CTX md5;
  MD5_Init(&md5);
  while(0 != (r = fread(buffer, 1, sizeof(buffer), f)))
    MD5_Update(&md5, buffer, r);
  unsigned char md5_digest[MD5_DIGEST_LENGTH];
  MD5_Final(md5_digest, &md5);
  logd("MD5 of '%s':", path);
  for(int i = 0; i < MD5_DIGEST_LENGTH; i++){
    logd("%X ",  md5_digest[i]);
  }
  logd("\n");
  
}

int main(int argc, char ** argv){
  file_md5(argv[1]);
  return 0;

  signal(SIGINT, handle_sigint);
  
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      udpc_file_serve(c2, NULL);
    }
    udpc_logout(con);
  }else if(argc > 3){

    int delay = 40;
    int bufsize = 1500;
    char * in_file = argv[2];
    char * out_file = argv[3];
    if(argc > 4)
      sscanf(argv[4],"%i", &delay);
    if(argc > 5)
      sscanf(argv[5],"%i", &bufsize);
    logd("Delay: %i, buffer size: %i file: '%s' '%s'\n", delay, bufsize, in_file, out_file);
    udpc_connection * con = udpc_connect(argv[1]);
    if(con != NULL){
      udpc_file_client(con, delay, bufsize, in_file, out_file);
      udpc_close(con);
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
