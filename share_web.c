#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>
#include <iron/types.h>
#include <iron/utils.h>
#include <iron/log.h>
#include <iron/fileio.h>
#include <iron/mem.h>
#include <iron/linmath.h>
#include <iron/time.h>

#include <microhttpd.h>

typedef struct{
  bool request_quit;
}web_context;

bool compare_strs(char * s1, char * s2){
  while(*s1 == *s2 && *s1 && *s2){
    s1++; s2++;
  }
  if(*s1 == 0) return true;
  return false;
}

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



int web_main(void * _ed, struct MHD_Connection * con, const char * url,
		      const char * method, const char * version,
		      const char *upload_data, size_t * upload_data_size,
		     void ** con_cls){
  UNUSED(url); UNUSED(version); UNUSED(upload_data); UNUSED(upload_data_size);
  UNUSED(method); UNUSED(con_cls); UNUSED(_ed);
  bool style_loc = compare_strs((char *) "style.css", (char *) url + 1);
  char * file = (char *) (style_loc ?  "style.css" : "page.html");
  logd("'%s'\n", url);
  if(url == strstr(url, "/shares/")){
    file = (char *) url + 1;
  }

  char * pg = read_file_to_string(file);
  struct MHD_Response * response = MHD_create_response_from_data(strlen(pg),
					   pg,
					   1,
					   MHD_NO);
  int ret = MHD_queue_response(con, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;
}

int main(){
  web_context ctx = {false};

  MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, 8000, NULL, NULL, &web_main, &ctx,
			   MHD_OPTION_END);
  while(false == ctx.request_quit)
    iron_usleep(100000);
}
