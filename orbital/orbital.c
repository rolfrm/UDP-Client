#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/stat.h>
#include <sys/time.h>
#include <iron/types.h>
#include <iron/process.h>
#include <iron/time.h>
#include <iron/mem.h>
#include <iron/log.h>
#include <iron/utils.h>
#include <iron/datastream.h>
#include <udpc.h>
#include "orbital.h"
#include <xxhash.h>
static void ensure_buffer_size(void ** ptr_to_buffer, size_t * current_size, size_t wanted_size){
  if(*current_size < wanted_size){
    *current_size = wanted_size;
    *ptr_to_buffer = realloc(*ptr_to_buffer, wanted_size);
  }
}

// get the time in microseconds, because nanoseconds is probably too accurate.
u64 get_file_time(const struct stat * stati){
  return stati->st_mtime;
}

u64 get_file_time2(const char * path){
  struct stat st;
  stat(path, &st);
  return st.st_mtime * 1000000;
}

u64 orbital_file_hash2(FILE * f){
  char buffer[1024 * 4];
  int read = 0;
  var state = XXH64_createState();
  XXH64_reset(state, 0);
  while((read = fread(buffer, 1, sizeof(buffer), f))){
    XXH64_update(state, buffer, read);
  }

  u64 hash = XXH64_digest(state);
  XXH64_freeState(state);
  return hash;
}

u64 orbital_file_hash(const char * file){
  FILE * f = fopen(file, "r");
  u64 hash = orbital_file_hash2(f);
  fclose(f);
  return hash;
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
  obj->target_rate = 1e5;
  obj->update_interval = 0.2;
  obj->active_conversation_count = 0;
  obj->is_processing = false;
  obj->is_updating = false;
  obj->connection_closed = false;
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
    let new_count = convId + 2;
    obj->conversations = ralloc(obj->conversations, new_count * sizeof(obj->conversations[0]));
    for(int i = obj->conversation_count; i < new_count; i++){
      obj->conversations[i] = NULL;
    }
    obj->conversation_count = new_count;
  }

  l = udpc_read(obj->connection, obj->read_buffer, obj->read_buffer_size);
  bool is_started = (convId % 2) == 1;
  
  conversation * conv = NULL;
  if(obj->conversations[convId / 2] == NULL){
    // new connection from peer.
    if(is_started)
      return;
    int mod = (convId / 2) % 2;

    if((obj->is_server && mod == 1) || (!obj->is_server && mod == 0)){
      
      ASSERT(obj->new_conversation != NULL);
	
      conv = alloc(sizeof(conversation));
      conv->talk = obj;
      conv->user_data = NULL;
      conv->finished = false;
      conv->id = convId;
      conv->process = NULL;
      conv->update = NULL;

      obj->new_conversation(conv, obj->read_buffer + 4, l - 4);
      if(conv->process == NULL || conv->update == NULL){
	dealloc(conv);
	return;
      }
      ASSERT(obj->conversations[convId/2] == NULL);
      obj->conversations[convId/2] = conv;
      conv->id = convId;
    }else
      return;
    //ERROR("Conversation deleted %i", l);
    
  }else{
    conv = obj->conversations[convId/2];
  }
  ASSERT(conv != NULL);
  conv->id = convId | 1;
  if(conv->finished == false)
    conv->process(conv, obj->read_buffer + 4, l - 4);

  if(timestamp() - obj->last_update > obj->update_interval * 1e6 ){
    obj->last_update = timestamp();
    int header = obj->is_server ? -2 : -3;
    talk_dispatch_send(obj, NULL, &header, sizeof(header));
  }
}

void talk_dispatch_process(talk_dispatch * talk, int timeoutms){
  udpc_connection * clst = talk->connection;

  if(udpc_wait_reads(&clst, 1, timeoutms) < 0 || clst == NULL)
    return;
  
  iron_mutex_lock(talk->process_mutex);
  ASSERT(talk->is_processing == false && talk->is_updating == false);
  talk->is_processing = true;
  while(true){
    _process_read(talk);
    if(udpc_wait_reads(&clst, 1, 0) < 0)
      break;
    if(clst == NULL)
      break;
    u8 buffer[5];
    if(udpc_peek(talk->connection, buffer, sizeof(buffer)) == 0){
      talk->connection_closed = true;
      break;
    }
  }
  
  talk->is_processing = false;
  iron_mutex_unlock(talk->process_mutex);
}

void talk_dispatch_update(talk_dispatch * talk){
  int count = 0;
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
      
      if(talk->conversations[i]->finished)
	goto finished;
      count++;
    }
  }
  
  for(size_t i = 0; i < talk->conversation_count; i++){
    if(talk->conversations[i] == NULL) continue;
    iron_mutex_lock(talk->process_mutex);
    ASSERT(talk->is_processing == false && talk->is_updating == false);
    talk->is_updating = true;
    _process_index(i);
    talk->is_updating = false;
    iron_mutex_unlock(talk->process_mutex);
  }
  talk->active_conversation_count = count;
}

conversation * talk_create_conversation(talk_dispatch * talk){
  iron_mutex_lock(talk->process_mutex);
  conversation * c = alloc(sizeof(conversation));
  c->talk = talk;
  c->user_data = NULL;
  c->finished = false;
  for(size_t i = (talk->is_server ? 0 : 1) + 2; i < talk->conversation_count; i += 2){
    if(talk->conversations[i] == NULL){
      c->id = i * 2;
      talk->conversations[i] = c;
      talk->active_conversation_count += 1;
      iron_mutex_unlock(talk->process_mutex);
      return c;
    }
  }

  talk->conversations = ralloc(talk->conversations, (talk->conversation_count + 4) * sizeof(talk->conversations[0]));
  for(size_t i = 0; i < 4; i++){
    talk->conversations[i + talk->conversation_count] = NULL;
  }

  
  c->id = (MAX(talk->conversation_count,2) + (talk->is_server ? 0 : 1)) * 2;
  
  talk->conversations[c->id / 2] = c;
  talk->conversation_count += 4;
  talk->active_conversation_count += 1;
  iron_mutex_unlock(talk->process_mutex);
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
      ASSERT(timespan > 0);
      
      if(current_rate >= target_rate){
	double waitTime = transferred / target_rate - timespan * 1e-6;
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
  memset(talk->send_buffer, 0, to_send);
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
  talk_dispatch_send(self->talk, self, message, message_length);
}

