#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include <iron/log.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <iron/process.h>
#include <iron/mem.h>
#include <iron/fileio.h>
#include <udpc.h>
#include "orbital.h"


void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  loge("%s\n", buffer);
  loge("Got error at %s line %i\n", file,line);
  raise(SIGINT);
}

udpc_service * udpc_login2(const char * url){
  udpc_service * s1 = NULL;
  while(s1 == NULL) s1 = udpc_login(url);
  return s1;
}

udpc_connection * udpc_listen2(udpc_service * con){
  udpc_connection * con2 = NULL;
  while(con2 == NULL) con2 = udpc_listen(con);
  return con2;
}

udpc_connection * udpc_connect2(udpc_service * _con, const char * url){
  UNUSED(_con);
  udpc_connection * con = NULL;
  while(con == NULL) con = udpc_connect(url);
  return con;
}

void test_conversation(){
  udpc_service * s1 = udpc_login2("test@0.0.0.0:s");
  udpc_service * s2 = udpc_login2("test@0.0.0.0:s2");
  logd("both login: %p %p\n", s1, s2);
  void count_up(conversation * self, void * data, int length){
    ASSERT(self->finished == false);
    ASSERT(self->talk->is_processing == true);
    ASSERT(self->talk->is_updating == false);
    int * d = (int *) data;
    //logd("count up: %i %i %i\n", d[0], length, self->id);
    ASSERT(d[0] == 123);
    if(length == 4){
      //logd("GOT END! %i\n", self->id);
      self->finished = true;
      return;
    }
    //logd("id: %i %i %i\n", self->id, self->talk->is_server, d[1]);
    if(d[1] == self->talk->is_server){
      ASSERT(length == 12);
      //logd("COUNT: %i\n", d[2]);
      if(d[2] < 500)
	conv_send(self, data, length);
      else{
	conv_send(self, data, 4);
	//logd("GOT END2! %i\n", self->id);
	self->finished = true;
      }
    }else{
      d[2] += 1;
      conv_send(self, data, length);
    }
  }

  
  void process_count(conversation * self){
    ASSERT(self->talk->is_processing == false);
    ASSERT(self->talk->is_updating == true);
    // todo: implement timeout for finished conversation or no response.
    if(self->user_data == NULL){
      self->user_data = (void *) 1;
      int data[] = {123, self->talk->is_server, 0};
      //logd("d: %i\n", sizeof(data));
      conv_send(self, data, sizeof(data));
    }
  }

  void * process_service(udpc_service * s){
    udpc_connection * con = NULL;
    if(s == s1)
      con = udpc_listen2(s);
    else
      con = udpc_connect2(s, "test@0.0.0.0:s");
    logd("Connected2 %p\n", s);

    talk_dispatch * talk = talk_dispatch_create(con, s == s1);
    logd("Server? %i\n", talk->is_server);
    void new_conversation(conversation * conv, void * buffer, int length)
    {

      ASSERT(length == 12);
      int * b = (int *) buffer;
      //logd("New conversation.. %i\n", b[0]);
      ASSERT(b[0] == 123);
      conv->user_data = (void *) 2;
      conv->update = process_count;
      conv->process = count_up;
      ASSERT(conv->id >= 2);
      
      //count_up(conv, buffer, length);

    }
    
    talk->new_conversation = new_conversation;
    void newconv(){
      iron_mutex_lock(talk->process_mutex);
      conversation * c = talk_create_conversation(talk);
      ASSERT(c->id != 0);
      c->update = process_count;
      c->process = count_up;
      iron_mutex_unlock(talk->process_mutex);
    }
    bool keep_processing = true;
    void process_talk(){
      while(keep_processing){
	talk_dispatch_process(talk, 0);
	//iron_usleep(1000);
      }
    }

    iron_thread * proc_trd = iron_start_thread0(process_talk);
    for(int i = 0; i < 10; i++)
      newconv();
    //newconv();
    //newconv();

    int _j = 0;
    while(_j < 10){
      talk_dispatch_update(talk);
      if(talk->active_conversation_count > 0)
	 _j = 0;
      else {
	_j++;
	iron_usleep(100);    
      }
      //iron_usleep(10000); //todo: try to comment this out.       
    }
    logd("DONE\n");
    keep_processing = false;
    iron_thread_join(proc_trd);
    talk_dispatch_delete(&talk);
    udpc_close(con);
    
    return NULL;
  }

  iron_thread * s1_trd = iron_start_thread((void * (*)()) process_service, s1);
  iron_thread * s2_trd = iron_start_thread((void * (*)()) process_service, s2);
  iron_thread_join(s1_trd);
  iron_thread_join(s2_trd);
  udpc_logout(s1);
  udpc_logout(s2);
}


void test_safesend(){
  udpc_service * s1 = udpc_login2("test@0.0.0.0:s");
  udpc_service * s2 = udpc_login2("test@0.0.0.0:s2");
  logd("both login: %p %p\n", s1, s2);

  
  
  void * process_service(udpc_service * s){

    void * recieve = NULL;
    size_t recieve_length = 0;
    size_t send_length = 10000;
    i8 * send = alloc(send_length * sizeof(send[0]));
    for(size_t i = 0; i <send_length; i++){
      i8 x = -(i % 123);
      send[i] = x;
    }

    udpc_connection * con = NULL;
    if(s == s1)
      con = udpc_listen2(s);
    else
      con = udpc_connect2(s, "test@0.0.0.0:s");
    talk_dispatch * talk = talk_dispatch_create(con, s == s1);
    writer * wt = mem_writer_create(&recieve, &recieve_length);
    reader * rd = mem_reader_create(send, send_length, false);
    void new_conversation(conversation * conv, void * buffer, int length)
    {
      UNUSED(buffer);
      UNUSED(length);
      if(((i8 *)buffer)[0] == 3)
	return;
      ASSERT(((i8 *)buffer)[0] != 3);
      safereceive_create(conv, wt);
    }
    
    talk->new_conversation = new_conversation;
    void newconv(){
      conversation * c = talk_create_conversation(talk);
      ASSERT(c->id != 0);
      safesend_create(c, rd);
    }
    bool keep_processing = true;
    void process_talk(){
      while(keep_processing)
	talk_dispatch_process(talk, 0);
    }

    iron_thread * proc_trd = iron_start_thread0(process_talk);
    newconv();

    int _j = 0;
    while(_j < 10){
      talk_dispatch_update(talk);
      if(talk->active_conversation_count > 0)
	 _j = 0;
      else {
	_j++;
	iron_usleep(100);    
      }
      iron_usleep(1000);
    }
    keep_processing = false;
    iron_thread_join(proc_trd);
    talk_dispatch_delete(&talk);
    udpc_close(con);
    for(size_t i = 0; i <send_length; i++){
      ASSERT(send[i] == ((i8 *)recieve)[i]);
    }
    
    return NULL;
  }

  iron_thread * s1_trd = iron_start_thread((void * (*)()) process_service, s1);
  iron_thread * s2_trd = iron_start_thread((void * (*)()) process_service, s2);
  iron_thread_join(s1_trd);
  iron_thread_join(s2_trd);
  udpc_logout(s1);
  udpc_logout(s2);



}

void test_reader(){
  size_t test_size = (rand() % 100000) + 50000;
  i8 * data = alloc(test_size);
  for(size_t i = 0; i < test_size; i++)
    data[i] = (i8) i;

  reader * rd = mem_reader_create(data, test_size, true);
  i8 buffer[10];
  for(size_t i = 0; i < test_size / 10; i++){
    reader_read(rd, buffer, sizeof(buffer));
    for(size_t j = 0; j < (size_t)array_count(buffer); j++){
      ASSERT(buffer[j] == (i8) (i * 10 + j));
    }
  }
  reader_seek(rd, 5 * 10);
  for(size_t i = 5; i < test_size / 10; i++){
    reader_read(rd, buffer, sizeof(buffer));
    for(size_t j = 0; j < (size_t)array_count(buffer); j++){
      ASSERT(buffer[j] == (i8) (i * 10 + j));
    }
  }

  reader_seek(rd, 0);


  
  const char * fname = "test_reader.file";
  writer * wt = file_writer_create(fname);
  size_t read = 0;
  char buf[100];

  void * reading = NULL;
  size_t reading_size = 0;
  writer *wt2 = mem_writer_create(&reading, &reading_size);
  
  while((read = reader_read(rd, buf, sizeof(buf)))){
    writer_write(wt,buf,read);
    writer_write(wt2, buf, read);
  }
  writer_close(&wt);
  writer_close(&wt2);

  ASSERT(reading_size == test_size);


  reader_close(&rd);

  rd = file_reader_create(fname);

  ASSERT(rd->size == reading_size);
  
  reader_seek(rd, 10 * 10);
  for(size_t i = 10; i < test_size / 10; i++){
    reader_read(rd, buffer, sizeof(buffer));
    for(size_t j = 0; j < (size_t)array_count(buffer); j++){
      ASSERT(buffer[j] == (i8) (i * 10 + j));
      ASSERT(buffer[j] == ((i8 *)reading)[i * 10 + j]);
    }
  }

  dealloc(reading);

  reader_close(&rd);

  remove(fname);
  
  ASSERT(rd == NULL);
}

void test_datalog(){
  system("rm -r sync_2");
  system("mkdir sync_2");
  system("rm sync_1/genfile");
  
  void * _userdata = (void*) 5;
  u64 cnt = 0;
  void f(const data_log_item_header * item, void * userdata){
    
    ASSERT(5 == (u64)userdata);
    logd("item: %p %i\n", item->file_id, item->type);
    cnt++;
  }
  data_log_generate_items("sync_1", f, _userdata);

  datalog dlog;

  datalog dlog2;
  datalog_initialize(&dlog2, "sync_2", "datalog_file_2", "commits_file_2");  

  remove(dlog2.commits_file);
  remove(dlog2.datalog_file);
  
  datalog_initialize(&dlog, "sync_1", "datalog_file", "commits_file");

  
  remove(dlog.commits_file);
  remove(dlog.datalog_file);
  datalog_update(&dlog);
  datalog_update(&dlog2);
  u64 commit_count = datalog_get_commit_count(&dlog);
  
  ASSERT(commit_count == cnt);
  logd("COMMIT COUNT: %i\n", commit_count);
  datalog_iterator di;
  datalog_iterator_init(&dlog, &di);
  const data_log_item_header * header = NULL;
  bool first = true;
  cnt = 0;
  while((header = datalog_iterator_next(&di)) != NULL){
    logd("OK..\n");
    logd("%i\n", header->type);
    if(header->type == 3){
      data_log_name * dname = (data_log_name *) header;
      logd("%p\n", dname->name);
      logd("'%s'\n", dname->name);
      
    }
    if(!first){
      datalog_apply_item(&dlog2, header, false, true);
      u64 commit_count2 = datalog_get_commit_count(&dlog2);
      logd("Commit count 2: %i\n", commit_count2);
      ASSERT(commit_count2 == (cnt + 1));

    }
    else{

    }
    first = false;
    cnt++;
    
  }
  
  ASSERT(cnt == commit_count);
  datalog_iterator_destroy(&di);
  datalog_update_files(&dlog2);

  {
    var c6_1 = datalog_get_commit_count(&dlog2);
    datalog_update(&dlog2);
    var c6 = datalog_get_commit_count(&dlog2);
    ASSERT(c6_1 == c6);
  }
  

  u64 c1 = datalog_get_commit_count(&dlog);
  datalog_destroy(&dlog);
  datalog_initialize(&dlog, "sync_1", "datalog_file", "commits_file");
  datalog_update(&dlog);
  u64 c1_2 = datalog_get_commit_count(&dlog);
  ASSERT(c1 == c1_2);
  u64 c2 = datalog_get_commit_count(&dlog2);
  ASSERT(c1 == c2);
  {
    datalog_commit_iterator it[2];
    for(int i = 0; i < 2; i++){
      datalog_commit_iterator_init(it,&dlog,  i == 1);
      datalog_commit_iterator_init(it + 1,&dlog2,  i == 1);
      commit_item item;
      commit_item item2;
      u64 c3 = 0;
      while(true){
	var ok1 = datalog_commit_iterator_next(it, &item);
	var ok2 = datalog_commit_iterator_next(it + 1, &item2);
	ASSERT(ok1 == ok2);
	if(!ok1)
	  break;
	c3 += 1;
	ASSERT(item.hash == item2.hash);
	ASSERT(item.length == item2.length);
      }
      ASSERT(c3 == c1);
    }
  }


  
  datalog_update(&dlog);
  write_string_to_file("11223344555", "sync_1/genfile");
  logd("----\n");
  u64 c4_pre = datalog_get_commit_count(&dlog);
  datalog_update(&dlog);
  u64 c4 = datalog_get_commit_count(&dlog);
  ASSERT(c4 == c4_pre + 3);

  ASSERT(c4 > c1);
  logd("C4: %i pre: %i\n", c4, c4_pre);
  system("sync");
  
  write_string_to_file("11223344555666", "sync_1/genfile");
  system("sync");
  
  datalog_update(&dlog);
  var c5 = datalog_get_commit_count(&dlog);
  logd("C4: %i %i\n", c4, c5);
  ASSERT(c5 > c4);

  //write_string_to_file("11223344555666777", "sync_2/genfile2");
  //datalog_update(&dlog2);
  write_string_to_file("3322111223344555666", "sync_2/genfile54");
  system("sync");
  datalog_update(&dlog2);
  var c6_1 = datalog_get_commit_count(&dlog2);
  datalog_update(&dlog2);
  var c6 = datalog_get_commit_count(&dlog2);
  ASSERT(c6_1 == c6);
  logd("C6: %i %i %i\n", c6, c5, c6_1);
  
  u64 search_max = MIN(datalog_get_commit_count(&dlog2), datalog_get_commit_count(&dlog)) - 1;
  logd("SEARCH MAX: %i %i %i\n", search_max, datalog_get_commit_count(&dlog2), datalog_get_commit_count(&dlog));
  while(search_max>0){
    var c1 = datalog_get_commit(&dlog, search_max);
    var c2 = datalog_get_commit(&dlog2, search_max);
    logd("%p %p\n", c1.hash, c2.hash);
    if(c1.hash == c2.hash && c1.length == c2.length && search_max)
      break;
    search_max -= 1;
  }
  logd("Found commit: %i\n", search_max);
  ASSERT(search_max > 0);


  {
    const char * buff_file = "sync2_buffer";
    UNUSED(buff_file);
    var c1 = datalog_get_commit(&dlog, search_max);
    
    
    {
      datalog_iterator it2;
      datalog_iterator_init_from(&it2, &dlog2, c1);
      const data_log_item_header * hd = datalog_iterator_next(&it2);
      FILE * buffer_file = fopen(buff_file, "w");
      ASSERT(buffer_file);
      while((hd = datalog_iterator_next(&it2)) != NULL){
	logd("writing to file: %i\n", hd->type);
	datalog_cpy_to_file(&dlog2, hd, buffer_file);
      }
      fclose(buffer_file);
      datalog_iterator_init_from(&it2, &dlog2, c1);
      datalog_rewind_to(&dlog2, &it2);
    }
    
    datalog_iterator it;
    datalog_iterator_init_from(&it, &dlog, c1);


    
    const data_log_item_header * hd = datalog_iterator_next(&it);
    
    logd("???? %p\n", hd);
    while((hd = datalog_iterator_next(&it)) != NULL){
      logd("%i\n", hd->type);
      datalog_apply_item(&dlog2, hd, false, true);
    }

    void apply_item_from_backup(const data_log_item_header * item, void * userdata){
      UNUSED(userdata);
      datalog_apply_item(&dlog2, item, false, true);
    }
    
    datalog_generate_from_file(buff_file, apply_item_from_backup, NULL);
  }
  
  ASSERT(c6 > c5);
  
  datalog_destroy(&dlog);
  datalog_destroy(&dlog2);  
}


// UDPC sample program.
int main(int argc, char ** argv){
  UNUSED(argc);
  UNUSED(argv);

  for(size_t i = 0; i < 10; i++){
    test_reader();
    logd("OK %i\n", i);
  }
  
  
  void run_server(){
    
    udpc_start_server("0.0.0.0");
  }

  iron_thread * srvtrd = iron_start_thread0(run_server);
  UNUSED(srvtrd);
  iron_usleep(10000);
  logd("test_conversation\n");
  //for(int i = 0; i < 1; i++)
    //test_conversation();
  logd("test_safesend\n");
  for(int i = 0; i < 1; i++)
    test_safesend();

  logd("Test DATALOG\n");
  for(int i = 0; i < 10; i++)
    test_datalog();
  
  return 0;
}
