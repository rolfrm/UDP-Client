#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>

#include <iron/log.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <iron/process.h>
#include <iron/mem.h>
#include <iron/fileio.h>
#include <iron/datastream.h>
#include <iron/datastream_server.h>
#include <udpc.h>

#include "orbital.h"
static data_stream weblog = {.name = "orbital_test"};

datastream_server * dserv = NULL;

void _error(const char * file, int line, const char * msg, ...){
  char buffer[1000];  
  va_list arglist;
  va_start (arglist, msg);
  vsprintf(buffer,msg,arglist);
  va_end(arglist);
  iron_sleep(0.2);
  if(dserv != NULL)
    datastream_server_flush(dserv);
  iron_sleep(0.2);
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

    void process_receive(conversation * conv, void * buffer, int length){
      safereceive_process(conv, conv->user_data, buffer, length);
    }

    void update_receive(conversation * conv){
      safereceive_update(conv, conv->user_data);
    }


    void process_send(conversation * conv, void * buffer, int length){
      safesend_process(conv, conv->user_data, buffer, length);
    }

    void update_send(conversation * conv){
      safesend_update(conv, conv->user_data);
    }
    
    void new_conversation(conversation * conv, void * buffer, int length)
    {
      dmsg(dlog_verbose,  "Starting new conversation: %i\n", conv->id);
      UNUSED(buffer);
      UNUSED(length);
      ASSERT(((i8 *)buffer)[0] != 3);
      conv->user_data = safereceive_create(wt);
      conv->process = process_receive;
      conv->update = update_receive;
    }
    void on_finished(conversation * conv){
      dmsg(dlog_verbose,  "Conversation finished: %i\n", conv->id);
    }
    
    talk->new_conversation = new_conversation;
    void newconv(){
      conversation * c = talk_create_conversation(talk);
      c->on_finished = on_finished;
      ASSERT(c->id != 0);
      dmsg(dlog_verbose,  "Creating new conversation: %i\n", c->id);
      c->user_data = safesend_create(rd);
      c->process = process_send;
      c->update = update_send;
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
  remove("sync_1/genfile54"); // sometimes generated by merge. Needs to be removed early.
  remove("sync_1/empty_file");
  void * _userdata = (void*) 5;
  u64 cnt = 0;
  void f(const data_log_item_header * item, void * userdata){
    UNUSED(item);    
    ASSERT(5 == (u64)userdata);
    cnt++;

  }

  data_log_generate_items("sync_1", f, _userdata);
  ASSERT(cnt > 0);
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
    if(!first){
      datalog_apply_item(&dlog2, header, false, true, true);
      u64 commit_count2 = datalog_get_commit_count(&dlog2);
      ASSERT(commit_count2 == (cnt + 1));

    }
    else{

    }
    first = false;
    cnt++;
    
  }
  
  ASSERT(cnt == commit_count);
  datalog_iterator_destroy(&di);

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

  void do_end_check(){
    datalog_assert_is_at_end(&dlog);
    datalog_assert_is_at_end(&dlog2);

  }
  UNUSED(do_end_check);
  //do_end_check();
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
  ASSERT(c4 == c4_pre + 5);

  ASSERT(c4 > c1);

  system("sync");
  
  write_string_to_file("11223344555666", "sync_1/genfile");

  fclose(fopen("sync_1/empty_file", "a"));
  system("sync");
  
  datalog_update(&dlog);
  var c5 = datalog_get_commit_count(&dlog);
  ASSERT(c5 > c4);
  write_string_to_file("3322111223344555666", "sync_2/genfile54");
  var c6_2 = datalog_get_commit_count(&dlog2);
  datalog_update(&dlog2);
  
  var c6_1 = datalog_get_commit_count(&dlog2);
  datalog_update(&dlog2);
  var c6 = datalog_get_commit_count(&dlog2);
  ASSERT(c6_1 == c6);
  ASSERT(c6_2 < c6);

  // commits from dlog are merged into dlog2.
  void do_merge(datalog * dlog, datalog * dlog2){
    datalog_print_commits(dlog, false);
    datalog_print_commits(dlog2, false);
    u64 dlog_cnt(){
      return datalog_get_commit_count(dlog);
    }
    u64 dlog2_cnt(){
      return datalog_get_commit_count(dlog2);
    }

    u64 search_max = MIN(dlog2_cnt(), dlog_cnt()) - 1;

    while(search_max>0){
      var c1 = datalog_get_commit(dlog, search_max);
      var c2 = datalog_get_commit(dlog2, search_max);
      
      if(c1.hash == c2.hash && c1.length == c2.length && search_max)
	break;
      search_max -= 1;
    }
    ASSERT(search_max > 0);

    
    const char * buff_file = "sync2_buffer";
    remove(buff_file);

    if(false){
      // it might not be necessesary to handle this edge case.
      if(dlog_cnt() -1 == search_max)
	return; // nothing to do.
    }
    var c1 = datalog_get_commit(dlog, search_max);
    var c2 = datalog_get_commit(dlog2, search_max);
    if(dlog2_cnt() > (search_max + 1) && dlog_cnt() > (search_max + 1)){
      var c1 = datalog_get_commit(dlog, search_max +1);
      var c2 = datalog_get_commit(dlog2, search_max +1);
      ASSERT(c1.hash != c2.hash);
    }
    
    ASSERT(c1.hash == c2.hash);
    ASSERT(c1.length == c2.length);
    
    { // make rewind buffer.
      datalog_iterator it2;
      datalog_iterator_init_from(&it2, dlog2, c1);
      const data_log_item_header * hd = datalog_iterator_next(&it2);
      FILE * buffer_file = fopen(buff_file, "w");
      ASSERT(buffer_file);
      while((hd = datalog_iterator_next(&it2)) != NULL){
	datalog_cpy_to_file(dlog2, hd, buffer_file);
      }
      fclose(buffer_file);
      datalog_iterator_destroy(&it2);
      datalog_iterator_init_from(&it2, dlog2, c1);
      datalog_iterator_next(&it2);
      datalog_rewind_to(dlog2, &it2);
      datalog_iterator_destroy(&it2);
    }

    datalog_iterator it;
    datalog_iterator_init_from(&it, dlog, c1);
    
    const data_log_item_header * hd = datalog_iterator_next(&it);


    // when merging we should not check delete actions.
    // first of all a delete action might be replayed
    // second of all the deleted file might be deleted in two places
    // and hence would again cause double deletes.
    while((hd = datalog_iterator_next(&it)) != NULL){
      dmsg(dlog_verbose,  "applying a commit.. %i\n", hd->type);
      datalog_apply_item(dlog2, hd, false, true, true);
    }

    u64 mc1 = datalog_get_commit_count(dlog);
    u64 mc2 = datalog_get_commit_count(dlog2);
    ASSERT(mc1 == mc2);
    void apply_item_from_backup(const data_log_item_header * item, void * userdata){
      UNUSED(userdata);
      dmsg(dlog_verbose,  "re-applying a commit.. %i\n", item->type);
      datalog_apply_item(dlog2, item, false, true, false);
    }
    
    datalog_generate_from_file(buff_file, apply_item_from_backup, NULL);
    c5 = datalog_get_commit_count(dlog);
    c6 = datalog_get_commit_count(dlog2);
  }

  datalog_update(&dlog);
  datalog_update(&dlog2);
  
  do_merge(&dlog, &dlog2);
  ASSERT(c6 > c5);
  do_merge(&dlog2, &dlog);
  ASSERT(c6 == c5);

  void update_cycle(){
    dmsg(dlog_verbose, "DLOG UPDATE 1");
    datalog_update(&dlog);
    dmsg(dlog_verbose, "DLOG UPDATE 2");
    datalog_update(&dlog2);
    dmsg(dlog_verbose, "DLOG MERGE 1");
    do_merge(&dlog, &dlog2);
    dmsg(dlog_verbose, "DLOG MERGE 2");
    do_merge(&dlog2, &dlog);
  }
  
  u64 c6_p = c6;
  u64 c5_p = c5;
  for(int i = 0; i < 10; i++){
    update_cycle();
    //since they are already merged no change should be needed.
    ASSERT(c6_p == c6);
    ASSERT(c5_p == c5);
  }

  // test delete and restore.
  remove("sync_2/genfile54");
  iron_usleep(10000);
  update_cycle();
  ASSERT(orbital_file_exists("sync_1/genfile54") == false);
  write_string_to_file("3322111223344555666", "sync_2/genfile54");
  dmsg(dlog_verbose, "Created FILE AGAIN\n");
  update_cycle();
  ASSERT(orbital_file_exists("sync_1/genfile54"));

  
  datalog_destroy(&dlog);
  datalog_destroy(&dlog2);  
}


void run_persisted_dirs(){
  system("rm -r sync_2");
  system("mkdir sync_2");
  system("rm sync_1/genfile");
  datalog dlog;

  datalog dlog2;
  datalog_initialize(&dlog2, "sync_2", "datalog_file_2", "commits_file_2");  

  remove(dlog2.commits_file);
  remove(dlog2.datalog_file);
  
  datalog_initialize(&dlog, "sync_1", "datalog_file", "commits_file");
  remove(dlog.commits_file);
  remove(dlog.datalog_file);
  system("sync");

  
  // commits from dlog are merged into dlog2.
  void do_merge(datalog * dlog, datalog * dlog2){

    u64 dlog_cnt(){
      return datalog_get_commit_count(dlog);
    }
    u64 dlog2_cnt(){
      return datalog_get_commit_count(dlog2);
    }

    u64 search_max = MIN(dlog2_cnt(), dlog_cnt()) - 1;

    while(search_max>0){
      var c1 = datalog_get_commit(dlog, search_max);
      var c2 = datalog_get_commit(dlog2, search_max);
      if(c1.hash == c2.hash && c1.length == c2.length && search_max)
	break;
      search_max -= 1;
    }
    
    const char * buff_file = "sync2_buffer";
    remove(buff_file);

    if(false){
      // it might not be necessesary to handle this edge case.
      if(dlog_cnt() -1 == search_max)
	return; // nothing to do.
    }
    var c1 = datalog_get_commit(dlog, search_max);
    var c2 = datalog_get_commit(dlog2, search_max);
    if(dlog2_cnt() > (search_max + 1) && dlog_cnt() > (search_max + 1)){
      var c1 = datalog_get_commit(dlog, search_max +1);
      var c2 = datalog_get_commit(dlog2, search_max +1);
      ASSERT(c1.hash != c2.hash);
    }
    
    ASSERT(c1.hash == c2.hash);
    ASSERT(c1.length == c2.length);
    
    {
      datalog_iterator it2;
      datalog_iterator_init_from(&it2, dlog2, c1);
      const data_log_item_header * hd = datalog_iterator_next(&it2);
      FILE * buffer_file = fopen(buff_file, "w");
      ASSERT(buffer_file);
      while((hd = datalog_iterator_next(&it2)) != NULL){
	datalog_cpy_to_file(dlog2, hd, buffer_file);
      }
      fclose(buffer_file);
      datalog_iterator_destroy(&it2);
      datalog_iterator_init_from(&it2, dlog2, c1);
      datalog_iterator_next(&it2);
      datalog_rewind_to(dlog2, &it2);
      datalog_iterator_destroy(&it2);
    }

    datalog_iterator it;
    datalog_iterator_init_from(&it, dlog, c1);
    
    const data_log_item_header * hd = datalog_iterator_next(&it);//NULL;
    
    while((hd = datalog_iterator_next(&it)) != NULL){
      dmsg(dlog_verbose,  "applying a commit.. %i\n", hd->type);
      datalog_apply_item(dlog2, hd, false, true, true);
    }
    datalog_iterator_destroy(&it);
    u64 mc1 = datalog_get_commit_count(dlog);
    u64 mc2 = datalog_get_commit_count(dlog2);
    ASSERT(mc1 == mc2);
    void apply_item_from_backup(const data_log_item_header * item, void * userdata){
      UNUSED(userdata);
      dmsg(dlog_verbose,  "re-applying a commit.. %i\n", item->type);
      datalog_apply_item(dlog2, item, false, true, false);
    }
    
    datalog_generate_from_file(buff_file, apply_item_from_backup, NULL);
  }
  int iteration = 0;

  while(true){
    logd("Run persisted loop... %i\n", iteration++);    
    datalog_update(&dlog2);
    datalog_update(&dlog);
    do_merge(&dlog, &dlog2);
    do_merge(&dlog2, &dlog);
    iron_usleep(100000);
  }
  
  datalog_destroy(&dlog);
  datalog_destroy(&dlog2);  
}


void test_distributed(){
  udpc_service * s1 = udpc_login2("test3@0.0.0.0:s");
  udpc_service * s2 = udpc_login2("test3@0.0.0.0:s2");

  typedef struct {
    udpc_service * serv;
    const char * dir;
    const char * name;
    datalog dlog;
  }share_ctx;


  typedef struct{
    bool keep_processing;
    talk_dispatch * talk;
  }processing_context;


  typedef enum {
    PLZ_SEND_COMMITS = 2,
    
    
  }convtype;
  
    typedef struct{
      convtype type;
      size_t length;
      //u64 x,y,z,w,x2,y2,z2,w2;
    }convthing;
  
  
  dmsg(weblog, "both login: %p %p\n", s1, s2);
  // initializing sync
  //   --> Ask for changes
  //     --> Binary search for changes.
  //   --> Ask for most
  //  Notify on changes
  // only one thread should be writing to the log.
  // so one thread listens for update notificiations and send
  // log data.
  // another thread handles the writes related stuff.
  
  void * process_service(share_ctx * ctx){

    dmsg(weblog, "Starting ctx :%p", ctx);
    udpc_service * s = ctx->serv;
    remove(quickfmt("%s.datalog", ctx->name));
    remove(quickfmt("%s.commits", ctx->name));
    datalog_initialize(&ctx->dlog, ctx->dir, quickfmt("%s.datalog", ctx->name), quickfmt("%s.commits", ctx->name));
    datalog_update(&ctx->dlog);
    var dlog = &ctx->dlog;
    udpc_connection * con = NULL;
    if(s == s1)
      con = udpc_listen2(s);
    else
      con = udpc_connect2(s, "test3@0.0.0.0:s");
    talk_dispatch * talk = talk_dispatch_create(con, s == s1);
    typedef struct{
      void * recieve;
      size_t recieve_length;
      writer * wt;
      safereceive_data * rcv;
    }writer_data;
    
    void on_finished_rd(conversation * conv){
      writer_data * wd = conv->user_data;
      writer_close(&wd->wt);

      dmsg(weblog, "DONE RD len: %i", wd->recieve_length);
      convthing * c = wd->recieve;
      ASSERT(c != NULL);
      dmsg(weblog, "on_finished_rd %p %p\n", c->type, c->length);
      dealloc(wd);
    }

    typedef struct{
      safesend_data * snd;
      reader * rd;
    }sender_reader;

    void process_receive(conversation * conv, void * buffer, int length){
      if(length > 8){
	u64 * x = buffer + length - 8;
	dmsg(weblog, "RX DATA %i %p\n", length, x[0] );
      }
      dmsg(weblog, "RCV %i -> %i\n", length, ((writer_data *)conv->user_data)->recieve_length );
      safereceive_process(conv, ((writer_data *)conv->user_data)->rcv, buffer, length);
    }

    void update_receive(conversation * conv){
      safereceive_update(conv, ((writer_data *)conv->user_data)->rcv);
    }


    void process_send(conversation * conv, void * buffer, int length){

      
      safesend_process(conv, ((sender_reader *) conv->user_data)->snd, buffer, length);
    }

    void update_send(conversation * conv){
      safesend_update(conv, ((sender_reader *) conv->user_data)->snd);
    }

    void new_conversation(conversation * conv, void * buffer, int length)
    {
      UNUSED(length);
      ASSERT(((i8 *)buffer)[0] != 3);
      writer_data * wd = alloc0(sizeof(wd[0]));
      writer * wt = mem_writer_create(&wd->recieve, &wd->recieve_length);
      wd->wt = wt;
      wd->rcv = safereceive_create(wt);
      conv->user_data = wd;
      conv->on_finished = on_finished_rd;
      conv->process = process_receive;
      conv->update = update_receive;
    }
    
    talk->new_conversation = new_conversation;


    
    void on_finished_wd(conversation * conv){
      UNUSED(conv);
    }
    
    void newconv(convthing * to_send){
      conversation * c = talk_create_conversation(talk);
      c->on_finished = on_finished_wd;
      ASSERT(c->id != 0);
      sender_reader * snd_rd = alloc0(sizeof(snd_rd[0]));
      convthing * loc = IRON_CLONE(to_send[0]);
      dmsg(weblog, "Sending thing of size: %i %p %p", sizeof(*to_send), ((u64 *) loc)[0], loc);
      reader * rd = mem_reader_create(loc, sizeof(convthing), true);
      safesend_data * snd = safesend_create(rd);
      snd_rd->rd = rd;
      snd_rd->snd = snd;
      c->user_data = snd_rd;
      c->process = process_send;
      c->update = update_send;
    }

    processing_context * cx = alloc0(sizeof(cx[0]));
    cx->keep_processing = true;
    cx->talk = talk;
    void * process_talk(processing_context * ctx){
      while(ctx->keep_processing)
	talk_dispatch_process(ctx->talk, 0);
      return NULL;
    }


    convthing to_send = {0};//.type = PLZ_SEND_COMMITS, .length = 10};
    to_send.type = PLZ_SEND_COMMITS;
    to_send.length = 10;
    iron_thread * proc_trd = iron_start_thread((void *)process_talk, (void *)cx);

    newconv(&to_send);
    dmsg(weblog, "Sending packet :%p", &to_send);

    int _j = 0;
    while(_j < 10){
      talk_dispatch_update(talk);
      if(talk->active_conversation_count > 0)
	 _j = 0;
      else {
	_j++;
	iron_usleep(100);    
      }
      iron_sleep(0.01);
      var search_max = datalog_get_commit_count(dlog);
      if(search_max > 0){
	var c1 = datalog_get_commit(dlog, search_max - 1);
	dmsg(weblog, "Update: %i", c1);
      }
    }
    cx->keep_processing = false;
    iron_thread_join(proc_trd);
    talk_dispatch_delete(&talk);
    udpc_close(con);
    
    return NULL;
  }

  share_ctx share1 = {.serv = s1, .dir = "sync_5", .name = "sync5"};
  share_ctx share2 = {.serv = s2, .dir = "sync_6", .name = "sync6"};
  
  iron_thread * s1_trd = iron_start_thread((void * (*)()) process_service, &share1);
  iron_thread * s2_trd = iron_start_thread((void * (*)()) process_service, &share2);
  iron_thread_join(s1_trd);
  iron_thread_join(s2_trd);
  udpc_logout(s1);
  udpc_logout(s2);
}



// UDPC sample program.
int main(int argc, char ** argv){
  UNUSED(argc);
  UNUSED(argv);
  dserv = datastream_server_run();
  

  
  datastream_server_wait_for_connect(dserv);
  //iron_sleep(200000);
  for(size_t i = 0; i < 10; i++){
    test_reader();
    dmsg(weblog, "OK %i\n", i);
    iron_usleep(100000);
  }
  
  
  
  void run_server(){
    
    udpc_start_server("0.0.0.0");
  }

  iron_thread * srvtrd = iron_start_thread0(run_server);
  UNUSED(srvtrd);
  iron_usleep(10000);
  dmsg(weblog, "test_conversation");
  //for(int i = 0; i < 1; i++)
    //test_conversation();
  logd("test_safesend\n");
  dmsg(weblog, "test safesend");
  for(int i = 0; i < 1; i++)
    test_safesend();
  dmsg(weblog, "Test DATALOG");
  for(int i = 0; i < 1; i++)
    test_datalog();
  dmsg(weblog, "TEST distributed");
  //run_persisted_dirs();
  test_distributed();
  if(dserv != NULL){
    iron_sleep(0.3); 
    datastream_server_flush(dserv);
    iron_sleep(0.3); 
  }

  
  return 0;
}
