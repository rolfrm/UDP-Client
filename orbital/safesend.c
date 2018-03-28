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

// conversation for sending a number of byte safely.
// This works by first sending the number of chunks and chunk size
// then each of the chunks are streamed in order with an id in the header
// then the safereceive will ask for chunks.


typedef struct{
  reader * rd;
  u8 * completed_chunks;
  size_t length;
  u32 initial_send;
  void * buffer;
  size_t buffer_length;
  size_t chunk_size;
  bool started;
  u64 last_send;
  
}safesend_data;

typedef struct {
  writer * wt;
  u8 * completed_chunks;
  size_t length;
  size_t received_blocks;
  size_t chunk_size;
  size_t chunk_count;

  u64 last_send;
  u64 last_recv;
  u64 finish_time;

  u32 * chunks_buffer;
  u32 chunk_buffer_count;
  
}safereceive_data;

typedef enum{
  SEND_HANDSHAKE = 1,
  RECV_HANDSHAKE_OK = 2,
  SEND_CHUNK = 3,
  RECV_REQ = 4,
  RECV_STAT = 5,
  RECV_FINAL = 6,
  SEND_FINAL = 7
}SAFESEND_HEADER;

static void send_header(conversation * self, i8 header){
  conv_send(self, &header, sizeof(header));
}

void pack_i8(void * buffer, size_t * index, i8 value){
  memmove(buffer + *index, &value, sizeof(value));
  *index  += sizeof(value);
}

void pack_u32(void * buffer, size_t * index, u32 value){
  memmove(buffer + *index, &value, sizeof(value));
  *index  += sizeof(value);
}

void pack_u64(void * buffer, size_t * index, u64 value){
  memmove(buffer + *index, &value, sizeof(value));
  *index  += sizeof(value);
}

i8 unpack_i8(void * buffer, size_t * index){
  i8 value;
  memmove(&value, buffer + *index, sizeof(value));
  *index  += sizeof(value);
  return value;
}

u32 unpack_u32(void * buffer, size_t * index){
  u32 value;
  memmove(&value, buffer + *index, sizeof(value));
  *index  += sizeof(value);
  return value;
}

u64 unpack_u64(void * buffer, size_t * index){
  u64 value;
  memmove(&value, buffer + *index, sizeof(value));
  *index  += sizeof(value);
  return value;
}


void safesend_send_chunk(conversation * self, u32 index){
  safesend_data * send = self->user_data;
  i8 header = SEND_CHUNK;
  size_t pack_idx = 0;
  pack_i8(send->buffer, &pack_idx, header);
  pack_u32(send->buffer, &pack_idx, index);
  reader_seek(send->rd, index * send->chunk_size);
  pack_idx += reader_read(send->rd, send->buffer + pack_idx, send->chunk_size);
  conv_send(self, send->buffer, pack_idx);
}


void safesend_update(conversation * self){
  safesend_data * send = self->user_data;
  if(send->started){
    if(send->initial_send != send->length){
      safesend_send_chunk(self, send->initial_send);
      send->initial_send += 1;
      return;
    }
  }
  else if(timestamp() - send->last_send > self->talk->latency * 10) {
    size_t pack_idx = 0;
    i8 header = SEND_HANDSHAKE;
    
    pack_i8(send->buffer, &pack_idx, header);
    pack_u64(send->buffer, &pack_idx, send->length);
    pack_u32(send->buffer, &pack_idx, send->chunk_size);
    conv_send(self, send->buffer, pack_idx);
    send->last_send = timestamp();
    return;
  }
}

void safesend_process(conversation * self, void * buffer, int size){
  safesend_data * send = self->user_data;
  size_t index = 0;
  SAFESEND_HEADER header = unpack_i8(buffer, &index);
  if(header == RECV_REQ){
    u32 * chunks = (buffer + index);
    size -= index;
    size_t count = size / sizeof(chunks[0]);
    for(size_t i = 0; i < count; i++){
      safesend_send_chunk(self, chunks[i]);
    }
  }
  if(header == RECV_HANDSHAKE_OK){
    send->started = true;
  }
  if(header == RECV_FINAL){
    send_header(self, SEND_FINAL);
    self->finished = true;
  }
}

void safesend_close(conversation * self){
  safesend_data * send = self->user_data;
  reader_close(&send->rd);
  dealloc(send);
  self->user_data = NULL;
}

void safesend_create(conversation * conv, reader * reader){
  safesend_data * send = alloc(sizeof(send[0]));
  send->rd = reader;
  send->length = reader->size;
  send->initial_send = 0;
  send->chunk_size = 1400;
  send->buffer_length = send->chunk_size * 2;
  send->buffer = alloc(send->buffer_length);
  send->started = false;
  send->last_send = 0;
  conv->user_data = send;

  conv->process =safesend_process;
  conv->update = safesend_update;
}

void safereceive_update(conversation * self){
  safereceive_data * send = self->user_data;
  if(send->received_blocks == send->chunk_count && send->finish_time == 0){
    send->finish_time = timestamp();
  }
  
  if(send->received_blocks == send->chunk_count && timestamp() - send->last_send > self->talk->latency * 4){
    send_header(self, RECV_FINAL);
    send->last_send = timestamp();
  }
  if(send->received_blocks == send->chunk_count && timestamp() - send->finish_time > self->talk->latency * 20){
    // SEND_FINAL got lost.
    self->finished = true;
  }
  if(send->last_recv > self->talk->latency * 10){
    u32 idx = 1;
    for(size_t i = send->received_blocks; i < send->chunk_count; i++){
      if(send->completed_chunks[i] == 0){
	send->chunks_buffer[idx] = i;
	idx += 1;
	if(idx == send->chunk_buffer_count)
	  break;
      }
    }
    if(idx > 0){
      send->chunks_buffer[0] = RECV_REQ;
      conv_send(self, send->chunks_buffer, idx * sizeof(send->chunks_buffer[0]));
      send->last_recv = timestamp() + self->talk->latency * 2; // don't do the same right again.
    }else{
      ASSERT(send->received_blocks == send->chunk_count);
    }
  }
}

void safereceive_process(conversation * self, void * buffer, int size){
  UNUSED(size);
  safereceive_data * send = self->user_data;
  send->last_recv = timestamp();
  size_t pack_index = 0;
  SAFESEND_HEADER header = unpack_i8(buffer, &pack_index);
  if(header == SEND_HANDSHAKE){
    size_t size = unpack_u64(buffer, &pack_index);
    size_t chunk_size = unpack_u32(buffer, &pack_index);
    size_t chunk_count = size / chunk_size + ((size % chunk_size == 0) ? 0 : 1);
    send->chunk_count = chunk_count;
    send->completed_chunks = alloc0(chunk_count * sizeof(send->completed_chunks[0]));
    send->length = size;
    send->received_blocks = 0;
    send->chunk_size = chunk_size;
    
    send_header(self, RECV_HANDSHAKE_OK);
    send->chunk_buffer_count = send->chunk_size / sizeof(send->chunks_buffer[0]);
    send->chunks_buffer = alloc(sizeof(send->chunks_buffer[0]) * send->chunk_buffer_count);

    
  }else if(header == SEND_CHUNK){
    u32 chunk_index = unpack_u32(buffer, &pack_index);
    writer_seek(send->wt, chunk_index * send->chunk_size);
    writer_write(send->wt, buffer + chunk_index, MIN(send->length - chunk_index * send->chunk_size, send->chunk_size));
    send->completed_chunks[chunk_index] = 1;
    if(chunk_index == send->received_blocks){
      send->received_blocks++;
      while(chunk_index <= send->chunk_count){
	if(send->completed_chunks[chunk_index]){
	  send->received_blocks++;
	  chunk_index++;
	}else{
	  break;
	}
      }
    }
    
  }else if(header == SEND_FINAL){
    self->finished = true;
  }
}

void safereceive_close(conversation * self){
  safereceive_data * send = self->user_data;
  if(send->completed_chunks != NULL){
    dealloc(send->completed_chunks);
  }
  if(send->chunks_buffer != NULL)
    dealloc(send->chunks_buffer);
  dealloc(send);
  self->user_data = NULL;
  
}


void safereceive_create(conversation * conv, writer * writer){
  safereceive_data * send = alloc(sizeof(safereceive_data));
  send->wt = writer;
  send->completed_chunks = NULL;
  send->length = 0;
  send->last_send = 0;
  send->last_recv = timestamp();
  send->finish_time = 0;
  send->chunks_buffer = NULL;
  send->chunk_buffer_count = 0;
  send->received_blocks = 0;

  conv->user_data = send;
  conv->process = safereceive_process;
  conv->update =safereceive_update;
}

