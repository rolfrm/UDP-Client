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
#include "udpc.h"
#include "udpc_utils.h"
#include "udpc_dir_scan.h"
#include "service_descriptor.h"

typedef struct{
  udpc_connection * connection;
  char * name;
  dirscan local;
  dirscan remote;
}connection;

typedef struct{
  bool request_quit;
  connection  * active_connections;
  size_t connection_cnt;
}web_context;

static connection * get_connection_by_name(web_context * ctx, const char * name){
  size_t * size = &ctx->connection_cnt;
  connection ** conns = &ctx->active_connections;
  size_t presize = *size;
  for(size_t i = 0; i < presize; i++)
    if(strcmp((*conns)[i].name, name) == 0){
      return &(*conns)[i];
    }
  return NULL;
}

static bool add_connection(web_context * ctx, const char * name, udpc_connection * con){
  size_t * size = &ctx->connection_cnt;
  connection** conns = &ctx->active_connections;
  size_t presize = *size;
  if(get_connection_by_name(ctx, name) != NULL){
    ERROR("Name already used by other connection\n");
    return false;
  }
  *conns = realloc(*conns, sizeof(connection) * (presize + 1));
  (*conns)[presize].connection = con;
  (*conns)[presize].name = fmtstr("%s", name);
  *size += 1;
  return true;	   
}

static void remove_connection(web_context * ctx, const char * name){
  size_t * size = &ctx->connection_cnt;
  connection * conns = ctx->active_connections;
  connection * con = get_connection_by_name(ctx, name);
  dealloc(con->name);
  ASSERT(con != NULL);
  size_t next_offset = ((con + 1) - conns);
  memmove(con, con + 1, (*size - next_offset) * sizeof(connection));
  *size -= 1;
}

bool test_connections(){
  web_context * ctx = alloc0(sizeof(web_context));
  for(int i = 0; i < 100; i++){
    char namebuf[100];
    sprintf(namebuf, "__test%i", i);
    add_connection(ctx, namebuf, (udpc_connection *) (size_t) i);
  }
  ASSERT(get_connection_by_name(ctx, "__test1"));
  remove_connection(ctx, "__test1");
  ASSERT(get_connection_by_name(ctx, "__test1") == NULL);
  ASSERT(get_connection_by_name(ctx, "__test14")->connection == (udpc_connection *) 14);
  dealloc(ctx->active_connections);
  dealloc(ctx);
  return true;
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

void update_dirfile(const char * dir, const char * name, const char * user){
  dirscan scan;
  char * target_dir;
  {
    char * current_dir = get_current_dir_name();
    chdir(dir);
    target_dir = get_current_dir_name();
    scan = scan_directories(".");
    chdir(current_dir);
  }
  
  {
    void * buffer = NULL;
    size_t cnt = 0;
    udpc_pack_string(dir, &buffer, &cnt);
    udpc_pack_string(name, &buffer, &cnt);
    udpc_pack_string(user, &buffer, &cnt);
    char * shareinfo_filename = fmtstr("shareinfo/%s", name);
    FILE * f = fopen(shareinfo_filename, "w");
    fwrite(buffer, 1, cnt, f);
    fclose(f);
    dealloc(shareinfo_filename);
  }
  ensure_directory("shares/");
  
  char * bin_filename = fmtstr("shares/%s.bin", name);
  size_t s = 0;
  void * b = dirscan_to_buffer(scan, &s);
  FILE * f = fopen(bin_filename, "w");
  fwrite(b, 1, s, f);
  fclose(f);
  dealloc(bin_filename);
  
  char * filename = fmtstr("shares/%s.json", name);
  FILE * jsonfile = fopen(filename, "w");
  fprintf(jsonfile, "{\"dir\": \"%s\", \"files\": [\n", target_dir);
  for(size_t i = 0; i < scan.cnt; i++){
    fprintf(jsonfile, "{ \"path\": \"%s\", \"md5\":\"", scan.files[i]);
    udpc_fprintf_md5(jsonfile, scan.md5s[i]);
    if(i != scan.cnt -1){
      fprintf(jsonfile, "\"},\n");
    }else{
      fprintf(jsonfile, "\"}\n");
    }
  }
  fprintf(jsonfile, "]}\n");
  fclose(jsonfile);
  dealloc(filename);
}

#include <ftw.h>

int web_main(void * _ed, struct MHD_Connection * con, const char * url,
		      const char * method, const char * version,
		      const char *upload_data, size_t * upload_data_size,
		     void ** con_cls){
  UNUSED(url); UNUSED(version); UNUSED(upload_data); UNUSED(upload_data_size);
  UNUSED(method); UNUSED(con_cls); UNUSED(_ed);
  const char * file = "page.html";

  bool style_loc = strcmp((char *) url + 1, "style.css") == 0;
  if(style_loc)
    file = "style.css";
  else if(0 == strcmp((char *) url + 1, (char *) "favicon.png"))
    file = "favicon.png";
  
  char fnamebuffer[100];
  logd("'%s' %s %s %i\n", url, method, version, upload_data_size);
  logd("File: %s\n", file);
  if(url == strstr(url, "/sharesinfo")){
    // Send back json code describing all the available shares.

    file = "shareinfo_data";
    
    dirscan dir = scan_directories("shareinfo");
    
    FILE * outfile = fopen(file, "w");
    fprintf(outfile, "[");
    for(size_t i = 0; i < dir.cnt; i++){
      logd("looking in: %s\n", dir.files[i]);
      void * rdbuffer = read_file_to_string(dir.files[i]);
      ASSERT(rdbuffer != NULL);
      void *ptr = rdbuffer;
      char * dirname = udpc_unpack_string(&ptr);
      char * name = udpc_unpack_string(&ptr);
      char * user = udpc_unpack_string(&ptr);
      fprintf(outfile, "{\"path\": \"%s\", \"name\":\"%s\", \"user\":\"%s\"}%s\n",dirname, name, user, (i == dir.cnt -1) ? "" : ",");
      dealloc(rdbuffer);
    }
    fprintf(outfile, "]");
    fclose(outfile);
    dirscan_clean(&dir);
  }else if(url == strstr(url,"/shares/")){
    // fetch the active shares item inside the shares folder
    
    char * shareid = (char *) url + strlen("/shares/");
    logd("Shareid: %s\n", shareid);
    char * shareinfo_filename = fmtstr("shareinfo/%s", shareid);
    void * buffer = read_file_to_string(shareinfo_filename);
    if(buffer == NULL)
      goto error;
    void * bufptr = buffer;
    char * dir = udpc_unpack_string(&bufptr);
    char * name = udpc_unpack_string(&bufptr);
    char * user = udpc_unpack_string(&bufptr);
    service_descriptor sd;
    if(!udpc_get_service_descriptor(user, &sd)){
      logd("Unable to parse service name: '%s'\n", user);
      dealloc(buffer);
      goto error;
    }

    logd("path: %s, name: %s, user: %s\n", dir, name, user);
    update_dirfile(dir, name, user);
    sprintf(fnamebuffer, "shares/%s.json", name);
    dealloc(buffer);
    logd("Sending: %s\n", fnamebuffer);
    file = fnamebuffer;
  }
  if(strcmp(url, "/add_share") == 0){ 
    const char * path = MHD_lookup_connection_value(con, MHD_GET_ARGUMENT_KIND, "path");
    const char * name = MHD_lookup_connection_value(con, MHD_GET_ARGUMENT_KIND, "name");
    const char * user = MHD_lookup_connection_value(con, MHD_GET_ARGUMENT_KIND, "user");
    logd("path: %s, name: %s, user: %s\n", path, name, user);
    if(path == NULL || name == NULL || user == NULL){
      goto error;
    }
    service_descriptor sd;
    if(!udpc_get_service_descriptor(user, &sd)){
      logd("Unable to parse service name: '%s'\n", user);
      goto error;
    }
    logd("Service descriptor seems ok \n");
    ensure_directory("shareinfo/");
    char * sharepath = fmtstr("shareinfo/%s", name);
    struct stat filest;
    stat(sharepath, &filest);
    dealloc(sharepath);
    
    if(S_ISREG(filest.st_mode)){
      logd("File exists!\n");
    }else{
      struct stat dirst;
      stat(path, &dirst);
      if(!S_ISDIR(dirst.st_mode)){
	logd("Dir does not exist.. creating a new one..\n");
	int path_len = strlen(path);
	if(path[path_len] !='/'){
	  char * npath = fmtstr("%s/", path);
	  ensure_directory(npath);
	  dealloc(npath);
	}else{
	  ensure_directory(path);
	}
      }
      logd("Updating dirfile!\n");
      update_dirfile(path, name, user);
      logd("Done..\n");
    }
    const char * r = "\"OK\"";
    struct MHD_Response * response = MHD_create_response_from_data(strlen(r),
								   (void *) r,
								   0,
								   MHD_NO);
    int ret = MHD_queue_response(con, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
  }

  size_t filesize = 0;
  void * pg = read_file_to_buffer(file, &filesize);
  struct MHD_Response * response = MHD_create_response_from_data(filesize,
					   pg,
					   1,
					   MHD_NO);
  int ret = MHD_queue_response(con, MHD_HTTP_OK, response);
  MHD_destroy_response(response);
  return ret;

 error:;
  const char * error_str = "<html><body>400</body></html>";
  response = MHD_create_response_from_data(strlen(error_str) + 1,
					   (void *) error_str,
					   0,
					   MHD_NO);
  ret = MHD_queue_response(con, MHD_HTTP_BAD_REQUEST, response);
  MHD_destroy_response(response);
  return ret;
}

void load_available_con(web_context * ctx){
  dirscan dir = scan_directories("shareinfo");
  for(size_t i = 0; i < dir.cnt; i++){
    char * f = dir.files[i];
    void * buffer = read_file_to_string(f);
    void * bufptr = buffer;
    char * dir = udpc_unpack_string(&bufptr);
    char * name = udpc_unpack_string(&bufptr);
    char * user = udpc_unpack_string(&bufptr);
    if(NULL != get_connection_by_name(ctx, name)){
      dealloc(buffer);
      continue;
    }
    logd(" %s %s %s \n", dir, name, user);
    udpc_connection * con = udpc_connect(user);
    if(con == NULL){
      dealloc(buffer);
      continue;
    }
    add_connection(ctx, name, con);
    connection * c = get_connection_by_name(ctx, name);
    c->local = scan_directories(dir);
    int ok = udpc_dirscan_client(con, &c->remote);
    if(ok < 0){
      logd("User %s\n", name);
      remove_connection(ctx, name);
    }
    dealloc(buffer);
  }
}

int main(int argv, char ** argc){
  if(argv > 1 && strcmp(argc[1], "--test") == 0){
    test_connections();
    logd("TEST SUCCESS\n");
    return 0;
  }
  web_context ctx = {0};
  load_available_con(&ctx);
  struct MHD_Daemon * d = MHD_start_daemon(MHD_USE_THREAD_PER_CONNECTION, 8000,
					   NULL, NULL, &web_main, &ctx, MHD_OPTION_END);
  logd("%i\n", d);
  while(false == ctx.request_quit){
    iron_usleep(100000);
    load_available_con(&ctx);
  }

}
