#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <iron/types.h>
#include <iron/mem.h>
#include <iron/utils.h>
#include <iron/process.h>
#include <iron/log.h>
#include <udpc.h>
#include "orbital.h"

typedef struct{
  FILE * f;
}file_reader_data;

typedef struct{
  void * ptr;
  size_t size;
  bool delete_on_close;
}mem_reader_data;

size_t file_reader_read(reader * rd, void * dst, size_t length){
  var f = ((file_reader_data *)rd->data)[0].f;
  size_t l = fread(dst,1,length, f);;
  return rd->position += l;
}

void file_reader_seek(reader * rd, size_t position){
  if(rd->position == position) return;
  var f = ((file_reader_data *)rd->data)[0].f;
  fseeko64(f, position, SEEK_SET);
  rd->position = position;
}

void file_reader_close(reader * rd){
  var f = ((file_reader_data *)rd->data)[0].f;
  fclose(f);
  dealloc(rd->data);
  dealloc(rd);
}


reader * file_reader_create(const char * path){
  var reader = (reader *) alloc(sizeof(reader));
  var fd = (file_reader_data *) alloc(sizeof(file_reader_data));
  fd->f = fopen(path, "r");
  ASSERT(fd->f);
  reader->data = fd;

  reader->read = file_reader_read;
  reader->seek = file_reader_seek;
  reader->close = file_reader_close;
  reader->position = 0;
  
  fseek(fd->f, 0L, SEEK_END);
  reader->size = ftell(fd->f);
  fseek(fd->f, 0L, SEEK_SET);
  
  return reader;
}

size_t mem_reader_read(reader * rd, void * dst, size_t size){
  var m = ((mem_reader_data *)rd->data)[0];
  i64 to_read = MIN((i64) m.size - rd->position, (i64) size);
  if(to_read <= 0) return 0;
  
  memmove(dst, m.ptr + rd->position, MAX(0, to_read));
  rd->position += to_read;
  return (size_t) MAX(0, to_read);
}

void mem_reader_close(reader * rd){
  var m = ((mem_reader_data *)rd->data)[0];
  if(m.delete_on_close)
    dealloc(m.ptr);
  dealloc(rd->data);
  dealloc(rd);
}

void mem_reader_seek(reader * rd, size_t position){
  rd->position = position;
}

reader * mem_reader_create(void * data, size_t size, bool delete_on_close){
  ASSERT(data);
  
  var reader = (reader *) alloc(sizeof(reader));
  var mem = (mem_reader_data *) alloc(sizeof(mem_reader_data));
  mem->ptr = data;
  mem->size = size;
  mem->delete_on_close = delete_on_close;
  reader->data = mem;

  reader->read = mem_reader_read;
  reader->seek = mem_reader_seek;
  reader->close = mem_reader_close;
  reader->position = 0;
  reader->size = size;
  return reader;
}

void reader_close(reader ** rd){
  reader * _rd = *rd;
  _rd->close(_rd);
  *rd = NULL;
}

size_t reader_read(reader * rd, void * buffer, size_t length){
  return rd->read(rd, buffer, length);
}

void reader_seek(reader * rd, size_t position){
  rd->seek(rd, position);
}

typedef struct{
  void ** ptr_location;
  size_t * size_location;
}mem_writer_data;

void mem_writer_write(writer * wt, void * src, size_t size){
  mem_writer_data data = ((mem_writer_data *) wt->data)[0];
  size_t required_size = wt->position + size;
  if(*data.size_location < required_size){
    data.ptr_location[0] = ralloc(data.ptr_location[0], required_size);
  }
  memmove(data.ptr_location[0], src, size);
  data.size_location[0] = required_size;
  wt->position += size;
}

void mem_writer_seek(writer * wt, size_t position){
  wt->position = position;
}

void mem_writer_close(writer * wt){
  dealloc(wt->data);
  dealloc(wt);
}

writer * mem_writer_create(void ** location, size_t * size_location){
  ASSERT(location != NULL);
  ASSERT(size_location != NULL);
  
  mem_writer_data d = {.ptr_location = location, .size_location = size_location};
  writer * wt = alloc(sizeof(writer));
  wt->data = IRON_CLONE(d);
  wt->write = mem_writer_write;
  wt->seek = mem_writer_seek;
  wt->close = mem_writer_close;
  wt->position = 0;
  return wt;
}

typedef struct{
  FILE * file;
}file_writer_data;

void file_writer_write(writer * wt, void * data, size_t size){
  var fw = ((file_writer_data *) wt->data)[0];
  fwrite(data, 1, size, fw.file);
  wt->position += size;
}

void file_writer_seek(writer * wt, size_t position){
  var fw = ((file_writer_data *) wt->data)[0];
  fseeko64(fw.file, position, SEEK_SET);
  wt->position = position;
}

void file_writer_close(writer * wt){
  var f = ((file_writer_data *)wt->data)[0].file;
  fclose(f);
  dealloc(wt->data);
  dealloc(wt);
}

writer * file_writer_create(const char * file){
  ASSERT(file != NULL);
  FILE * f = fopen(file, "w");
  file_writer_data fd = {.file = f};
  writer * wt = alloc(sizeof(writer));
  
  wt->data = IRON_CLONE(fd);
  wt->write = file_writer_write;
  wt->seek = file_writer_seek;
  wt->close = file_writer_close;
  wt->position = 0;
  return wt;
}

void writer_write(writer * wt, void * src, size_t size){
  wt->write(wt, src, size);
}
void writer_seek(writer * wt, size_t position){
  wt->seek(wt, position);
}
void writer_close(writer ** wt){
  (*wt)->close(*wt);
  *wt = NULL;
}
