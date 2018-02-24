OPT = -g3 -Og
LIB_SOURCES1 = udpc.c udp.c ssl.c service_descriptor.c udpc_utils.c udpc_stream_check.c udpc_send_file.c udpc_dir_scan.c udpc_seq.c udpc_share_log.c udpc_share_delete.c
LIB_SOURCES = $(addprefix src/, $(LIB_SOURCES1))
CC = gcc
TARGET = libudpc.so
LIB_OBJECTS =$(LIB_SOURCES:.c=.o)
LDFLAGS= -L. $(OPT) -Wextra #-lmcheck #-ftlo #setrlimit on linux 
LIBS= -ldl -lm -lssl -lcrypto -lpthread -liron
ALL= $(TARGET) server rpc speed file share test  share_log_reader share_manager libudpc_net #web dir_scanner
CFLAGS = -Iinclude -Isrc -std=c11 -c $(OPT) -D_GNU_SOURCE -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0  -fdiagnostics-color -Wextra -Werror -Wwrite-strings -fbounds-check  #-DDEBUG

$(TARGET): $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(LIBS) --shared -o $@

all: $(ALL)

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -fPIC $< -o $@ -MMD -MF $@.depends 
depend: h-depend
clean:
	rm -f $(LIB_OBJECTS) $(ALL) *.o.depends
	rm -f main.o udpc_get.o udpc_speed_test.o udpc_file.o udpc_share.o

-include $(LIB_OBJECTS:.o=.o.depends)

libudpc_net: $(LIB_OBJECTS) udpc_net.o
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) udpc_net.o $(LIBS) -liron --shared -o libudpc_net.so 

server: $(TARGET) src/main.o
	$(CC) $(LDFLAGS) $(LIBS) src/main.o -ludpc -Wl,-rpath,. -o server

rpc: $(TARGET) udpc_get.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_get.o -ludpc -Wl,-rpath,. -o rpc

speed: $(TARGET) udpc_speed_test.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_speed_test.o -ludpc -Wl,-rpath,. -o speed

file: $(TARGET) udpc_file.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_file.o -ludpc -Wl,-rpath,. -o file

share: $(TARGET) udpc_share.o
	$(CC) $(LDFLAGS) udpc_share.o $(LIBS) -ludpc -Wl,-rpath,. -o share

share2: $(TARGET) udpc_share2.o
	$(CC) $(LDFLAGS) udpc_share2.o $(LIBS) -ludpc -Wl,-rpath,. -o share2 -lgit2


share_log_reader: $(TARGET) udpc_share_log_reader.o
	$(CC) $(LDFLAGS) udpc_share_log_reader.o $(LIBS) -ludpc -Wl,-rpath,. -o share_log_reader	

share_manager: $(TARGET) udpc_share_manager.o
	$(CC) $(LDFLAGS) udpc_share_manager.o $(LIBS) -ludpc -Wl,-rpath,. -o share_manager

test: $(TARGET) src/udpc_test.o
	$(CC) $(LDFLAGS) src/udpc_test.o $(LIBS) -ludpc -Wl,-rpath,. -o test

#web: $(TARGET) share_web.o
#	$(CC) $(LDFLAGS) share_web.o $(LIBS) -lmicrohttpd -ludpc -Wl,-rpath,. -o #web
