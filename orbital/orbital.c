#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <iron/types.h>
#include <iron/process.h>
#include <iron/time.h>
#include <iron/mem.h>
#include <iron/log.h>
#include <iron/utils.h>

#include <udpc.h>
#include "orbital.h"
void print_buffer(void * buf, size_t length){
  for(size_t i = 0; i < length; i++){
    logd("%x", ((u8 *)buf)[i]);
  }
}
static void ensure_buffer_size(void ** ptr_to_buffer, size_t * current_size, size_t wanted_size){
  if(*current_size < wanted_size){
    *current_size = wanted_size;
    *ptr_to_buffer = realloc(*ptr_to_buffer, wanted_size);
  }
}

talk_dispatch * talk_dispatch_create(udpc_connection * con, bool is_server){
  ASSERT(con != NULL);
  talk_dispatch * obj = alloc(sizeof(talk_dispatch));
  obj->is_server = is_server;
  obj->connection = con;
  obj->read_buffer_size = 1024;
  obj->read_buffer = alloc(obj->read_buffer_size);
  obj->conversations = NULL;
  obj->conversation_count = 0;
  obj->send_buffer_size = 1024;
  obj->send_buffer = alloc(obj->send_buffer_size);
  obj->transfer_capacity = 64;
  obj->transfer_time_window = alloc(sizeof(obj->transfer_time_window[0]) * obj->transfer_capacity);
  obj->transfer_sum_window = alloc(sizeof(obj->transfer_sum_window[0]) * obj->transfer_capacity);
  obj->transfer_count = 0;
  obj->transfer_sum = 0;
  obj->transfer_index = 0;
  obj->process_mutex = iron_mutex_create();
  obj->last_update = 0;
  obj->latency = 1000000; //Assume 1s latency
  obj->target_rate = 1e4;
  obj->update_interval = 0.1;
  return obj;
}

static void _process_read(talk_dispatch * obj){
  int l = udpc_peek(obj->connection, obj->read_buffer, 5);
  if(l < 4 && l <= 0) return;
  ASSERT(l>=4);
  l = udpc_pending(obj->connection);
  ensure_buffer_size(&obj->read_buffer, &obj->read_buffer_size, l);

  int convId = ((int *) obj->read_buffer)[0];
  ASSERT(convId != 0 && convId != -1);

  { // latency packet handling.
    if(convId == (obj->is_server ? -2 : -3)){
      // special ping message
      // calculate latency
      udpc_read(obj->connection, obj->read_buffer, obj->read_buffer_size);
      obj->latency = timestamp() - obj->last_update;
      logd("Calculated latency: %f s\n", (obj->latency) * 1e-6);
      return;
    }
    if(convId == (obj->is_server ? -3 : -2)){
      int l = udpc_read(obj->connection, obj->read_buffer, obj->read_buffer_size);
      talk_dispatch_send(obj, NULL, obj->read_buffer, l);
      return;
    }
  }

  if(obj->conversation_count < (size_t)convId){
    obj->conversations = ralloc(obj->conversations, (convId + 2) * sizeof(obj->conversations[0]));
    for(int i = obj->conversation_count; i < (convId + 2); i++){
      obj->conversations[i] = NULL;
    }
    obj->conversation_count = convId + 2;
  }

  l = udpc_read(obj->connection, obj->read_buffer, obj->read_buffer_size);
  //logd("READ %i ", l);
  //print_buffer(obj->read_buffer, l);
  //logd("\n");

  conversation * conv = NULL;
  if(obj->conversations[convId] == NULL){
    // new connection from peer.
    int mod = convId % 2;
    logd("ConvID :%i %i\n", convId, obj->is_server);
    if((obj->is_server && mod == 1) || (!obj->is_server && mod == 0)){
      ASSERT(obj->new_conversation != NULL);
	
      conv = alloc(sizeof(conversation));
      conv->talk = obj;
      conv->user_data = NULL;
      conv->finished = false;
      conv->id = convId;

      obj->new_conversation(conv, obj->read_buffer + 4, l - 4);
      obj->conversations[convId] = conv;
      conv->id = convId;
    }else{
      ERROR("Conversation deleted");
    }
  }else{
    conv = obj->conversations[convId];
  }
  ASSERT(conv != NULL);
  
  conv->process(conv, obj->read_buffer + 4, l - 4);
  if(timestamp() - obj->last_update > obj->update_interval * 1e6 ){
    obj->last_update = timestamp();
    int header = obj->is_server ? -2 : -3;
    talk_dispatch_send(obj, NULL, &header, sizeof(header));
  }
}

void talk_dispatch_process(talk_dispatch * talk, int timeoutms){
  udpc_connection * clst = talk->connection;
  if(udpc_wait_reads(&clst, 1, timeoutms) == -1 || clst == NULL)
    return;
  iron_mutex_lock(talk->process_mutex);
  _process_read(talk);
  iron_mutex_unlock(talk->process_mutex);
}

void talk_dispatch_update(talk_dispatch * talk){
  void _process_index(size_t i){
    if(talk->conversations[i] != NULL){
      if(talk->conversations[i]->finished){
      finished:
	dealloc(talk->conversations[i]);
	talk->conversations[i] = NULL;
	return;
      }
      ASSERT(talk->conversations[i]->update != NULL);

      talk->conversations[i]->update(talk->conversations[i]);
      iron_mutex_unlock(talk->process_mutex);
      if(talk->conversations[i]->finished)
	goto finished;
    }
  }
  for(size_t i = 0; i < talk->conversation_count; i++){
    iron_mutex_lock(talk->process_mutex);
    _process_index(i);
    iron_mutex_unlock(talk->process_mutex);
    iron_mutex_unlock(talk->process_mutex);
  }
}

conversation * talk_create_conversation(talk_dispatch * talk){
  
  conversation * c = alloc(sizeof(conversation));
  c->talk = talk;
  c->user_data = NULL;
  c->finished = false;
  for(size_t i = (talk->is_server ? 0 : 1) + 2; i < talk->conversation_count; i += 2){
    if(talk->conversations[i] == NULL){
      c->id = i;
      talk->conversations[i] = c;
      return c;
    }
  }

  talk->conversations = ralloc(talk->conversations, (talk->conversation_count + 4) * sizeof(talk->conversations[0]));
  for(size_t i = 0; i < 4; i++)
    talk->conversations[i + talk->conversation_count] = NULL;

  
  c->id = MAX(talk->conversation_count,2) + (talk->is_server ? 0 : 1);
  
  talk->conversations[c->id] = c;
  talk->conversation_count += 4;
  return c;
}

void talk_dispatch_send(talk_dispatch * talk, conversation * conv, void * message, int message_size){

  int to_send = message_size + (conv == NULL ? 0 : sizeof(conv->id));
  
  { // check transfer rate.
  check_transfer_rate:;
    u64 ticks_now = timestamp();
    if(talk->transfer_count > 5){
      int index1 = (int)talk->transfer_index;
      int index2 = 0;
      if(talk->transfer_count == talk->transfer_capacity)
	index2 = (index1 + 1) % talk->transfer_count;
      i64 timespan = ticks_now - talk->transfer_time_window[index2];
      i64 transferred = talk->transfer_sum + to_send;
      double current_rate = transferred / (timespan * 1e-6);
      double target_rate = talk->target_rate;
      //logd("RATE: %f %f %i %i \n", current_rate, target_rate, timespan, transferred);
      ASSERT(timespan > 0);
      
      if(current_rate >= target_rate){
	double waitTime = transferred / target_rate - timespan * 1e-6;
	//logd("SLEEP: %f\n", waitTime);
	iron_sleep(waitTime);
	goto check_transfer_rate;
      }
    } 
    if(talk->transfer_count < talk->transfer_capacity){
      talk->transfer_index = talk->transfer_count;
      talk->transfer_count += 1;
      talk->transfer_time_window[talk->transfer_index] = ticks_now;
      talk->transfer_sum_window[talk->transfer_index] = to_send;
      talk->transfer_sum += to_send;
    }else{
      
      talk->transfer_index = (talk->transfer_index + 1) % talk->transfer_count;
      talk->transfer_sum = talk->transfer_sum - talk->transfer_sum_window[talk->transfer_index] + to_send;
      talk->transfer_sum_window[talk->transfer_index] = to_send;
    }
  }

  if(conv == NULL){
    udpc_write(talk->connection, message, to_send);
    return;
  }
  
  ensure_buffer_size(&talk->send_buffer, &talk->send_buffer_size, to_send);
  memmove(talk->send_buffer, &conv->id, sizeof(conv->id));
  memmove(talk->send_buffer + sizeof(conv->id), message, message_size);
  udpc_write(talk->connection, talk->send_buffer, to_send);
}

void talk_dispatch_delete(talk_dispatch ** _talk){
  talk_dispatch * talk = *_talk;
  *_talk = NULL;
  dealloc(talk->read_buffer);
  dealloc(talk->send_buffer);
  dealloc(talk->conversations);
  dealloc(talk->transfer_time_window);
  dealloc(talk->transfer_sum_window);
  dealloc(talk);
}

void conv_send(conversation * self, void * message, int message_length){
  //logd("Sending %i ", message_length);
  //print_buffer(message, message_length);
  //logd("\n");
  
  talk_dispatch_send(self->talk, self, message, message_length);
}

					  
					
