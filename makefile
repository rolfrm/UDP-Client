OPT = -g3 -O0
LIB_SOURCES = udpc.c udp.c ssl.c ../iron/mem.c ../iron/array.c  ../iron/math.c service_descriptor.c ../iron/time.c udpc_utils.c udpc_stream_check.c udpc_send_file.c
CC = gcc
TARGET = libudpc.so
LIB_OBJECTS =$(LIB_SOURCES:.c=.o)
LDFLAGS= -L.  $(OPT) -Wextra #-lmcheck #-ftlo  #setrlimit on linux 
LIBS= -ldl -lm -lssl -lcrypto -lpthread

CFLAGS =  -I.. -std=c11 -c $(OPT) -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0  -D_GNU_SOURCE -fdiagnostics-color #-Werror -Wwrite-strings #-DDEBUG

all: $(TARGET) test rpc speed file
$(TARGET): $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(LIBS) --shared -o $@

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -fPIC $< -o $@ -MMD -MF $@.depends 
depend: h-depend
clean:
	rm $(LIB_OBJECTS) $(TARGET) *.o.depends
-include $(LIB_OBJECTS:.o=.o.depends)

test: $(TARGET)
	$(CC) $(CFLAGS) main.c ../iron/log.c
	$(CC) $(LDFLAGS) $(LIBS) main.o log.o -ludpc -Wl,-rpath,. -o server.exe

rpc: $(TARGET)
	$(CC) $(CFLAGS) udpc_get.c ../iron/log.c
	$(CC) $(LDFLAGS) $(LIBS) udpc_get.o log.o  -ludpc -Wl,-rpath,. -o rpc.exe

speed: $(TARGET) ../iron/log.c udpc_speed_test.c
	$(CC) $(CFLAGS)  udpc_speed_test.c ../iron/log.c
	$(CC) $(LDFLAGS) $(LIBS) udpc_speed_test.o log.o  -ludpc -Wl,-rpath,. -o speed.exe

file:  $(TARGET) ../iron/log.c udpc_file.c
	$(CC) $(CFLAGS)  udpc_file.c ../iron/log.c
	$(CC) $(LDFLAGS) $(LIBS) udpc_file.o log.o  -ludpc -Wl,-rpath,. -o file.exe
