#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include <iron/log.h>
#include <iron/types.h>
#include <iron/time.h>
#include <iron/utils.h>
#include <iron/process.h>

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
    int * d = (int *) data;
    //logd("count up: %i %i\n", d[0], length);
    ASSERT(d[0] == 123);
    //logd("id: %i %i %i\n", self->id, self->talk->is_server, d[1]);
    if(d[1] == self->talk->is_server){
      ASSERT(length == 12);
      //logd("COUNT: %i\n", d[2]);
      if(d[2] < 1000)
	conv_send(self, data, length);
      else{
	conv_send(self, data, 4);
	self->finished = true;
      }
    }else{
      if(length == 4){
	self->finished = true;
      }else{
	d[2] += 1;
	conv_send(self, data, length);
      }
    }
  }

  
  void process_count(conversation * self){
    //logd("process..\n");
    // todo: implement timeout for finished conversation or no response.
    UNUSED(self);
    if(self->user_data == NULL){
      self->user_data = (void *) 1;
      int data[] = {123, self->talk->is_server, 0};
      logd("d: %i\n", sizeof(data));
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
      logd("New conversation.. %i\n", b[0]);
      ASSERT(b[0] == 123);
      conv->user_data = (void *) 2;
      conv->update = process_count;
      conv->process = count_up;
      ASSERT(conv->id >= 2);
      
      count_up(conv, buffer, length);

    }
    
    talk->new_conversation = new_conversation;
    conversation * c = talk_create_conversation(talk);
    ASSERT(c->id != 0);
    logd("ID: %i %i\n", c->id, talk->is_server);
    c->update = process_count;
    c->process = count_up;

    void process_talk(){
      while(true){
	talk_dispatch_process(talk, 1000);
      }
    }

    iron_thread * proc_trd = iron_start_thread0(process_talk);
    
    while(true){
      talk_dispatch_update(talk);
    }
    iron_thread_join(proc_trd);
    
    return NULL;
  }

  iron_thread * s1_trd = iron_start_thread((void * (*)()) process_service, s1);
  iron_thread * s2_trd = iron_start_thread((void * (*)()) process_service, s2);

  iron_thread_join(s1_trd);
  iron_thread_join(s2_trd);
  
}


// UDPC sample program.
int main(int argc, char ** argv){
  UNUSED(argc);
  UNUSED(argv);
  void run_server(){
    
    udpc_start_server("0.0.0.0");
  }

  iron_thread * srvtrd = iron_start_thread0(run_server);
  UNUSED(srvtrd);
  iron_usleep(10000);
  logd("test_conversation\n");
  test_conversation();

  
  return 0;
}