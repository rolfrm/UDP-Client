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

typedef struct{
  reader * rd;
  u8 * completed_chunks;
  size_t length;

}safesend_data;

typedef struct {
  writer * wt;
  u8 * completed_chunks;
  size_t length;
  size_t received_block;
}safereceive_data;

/*
// conversation for sending a number of byte safely.
void safesend_create(conversation * conv, reader * reader){
  
}

void safereceive_create(conversation * conv, writer * writer){

}*/
