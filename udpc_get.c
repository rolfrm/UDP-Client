// simple program to user UDPC to transfer a file between two peers
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "udpc.h"
#include "service_descriptor.h"
#include <iron/log.h>

#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
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

// UDPC sample program.
// usage:
// SERVER: udpc_get name@server:service
// CLIENT: udpc_get name@server:service [command]
int main(int argc, char ** argv){
  signal(SIGINT, handle_sigint);
  if(argc == 2){
    udpc_service * con = udpc_login(argv[1]);
    logd("Logged in..\n");
    while(!should_close){
      udpc_connection * c2 = udpc_listen(con);      
      if(c2 == NULL)
	continue;
      char buffer[1500]; //or max MTU size
      size_t r = 0;
      while(r == 0)
	r = udpc_read(c2,buffer, sizeof(buffer));
      FILE * fp = popen(buffer, "r");
      size_t read = 0;
      while(0 != (read = fread(buffer, 1, sizeof(buffer), fp))){
	udpc_write(c2, buffer, read);
	usleep(10);
      }
      udpc_write(c2, "ENDENDEND", 10);
      udpc_close(c2);
    }
    udpc_logout(con);
  }else if(argc > 2){
    udpc_connection * con = udpc_connect(argv[1]);
    size_t totalsize = 0;
    for(int i = 2; i < argc; i++){
      totalsize = strlen(argv[i]) + 1;
    }
    char outbuffer[1500];
    {
      char * ptr = outbuffer;
      for(int i = 2; i < argc; i++){
	strcpy(ptr, argv[i]);
	ptr += strlen(ptr);
	strcpy(ptr, " ");
	ptr += 1;
      }
      ASSERT(totalsize == strlen(outbuffer));
      udpc_write(con, outbuffer, totalsize + 1);
      while(true){
	size_t r = 0;
	while(r == 0)
	  r = udpc_read(con, outbuffer, sizeof(outbuffer));
	
	if(strcmp("ENDENDEND", outbuffer) == 0)
	  break;
	fwrite(outbuffer, 1, r, stdout);
      }
      udpc_close(con);
    }
  }else{
    loge("Missing arguments\n");
  }
  
  return 0;
}
