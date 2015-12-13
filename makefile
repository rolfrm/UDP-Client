OPT = -g3 -O0
LIB_SOURCES = udpc.c udp.c ssl.c ../iron/mem.c ../iron/array.c ../iron/math.c service_descriptor.c ../iron/time.c udpc_utils.c udpc_stream_check.c udpc_send_file.c udpc_dir_scan.c ../iron/log.c ../iron/fileio.c
CC = gcc
TARGET = libudpc.so
LIB_OBJECTS =$(LIB_SOURCES:.c=.o)
LDFLAGS= -L. $(OPT) -Wextra #-lmcheck #-ftlo #setrlimit on linux 
LIBS= -ldl -lm -lssl -lcrypto -lpthread
ALL= $(TARGET) server rpc speed file share test web dir_scanner
CFLAGS = -I.. -std=c11 -c $(OPT) -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0 -D_GNU_SOURCE -fdiagnostics-color -Wextra -Werror -Wwrite-strings #-DDEBUG

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

server: $(TARGET) main.o
	$(CC) $(LDFLAGS) $(LIBS) main.o -ludpc -Wl,-rpath,. -o server

rpc: $(TARGET) udpc_get.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_get.o -ludpc -Wl,-rpath,. -o rpc

speed: $(TARGET) udpc_speed_test.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_speed_test.o -ludpc -Wl,-rpath,. -o speed

file: $(TARGET) udpc_file.o
	$(CC) $(LDFLAGS) $(LIBS) udpc_file.o -ludpc -Wl,-rpath,. -o file

share: $(TARGET) udpc_share.o
	$(CC) $(LDFLAGS) udpc_share.o $(LIBS) -ludpc -Wl,-rpath,. -o share

test: $(TARGET) udpc_test.o
	$(CC) $(LDFLAGS) udpc_test.o $(LIBS) -ludpc -Wl,-rpath,. -o test

web: $(TARGET) share_web.o
	$(CC) $(LDFLAGS) share_web.o $(LIBS) -lmicrohttpd -ludpc -Wl,-rpath,. -o web
