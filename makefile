OPT = -g3 -O0
LIB_SOURCES = udpc.c udp.c ssl.c ../iron/mem.c ../iron/array.c ../iron/log.c ../iron/math.c service_descriptor.c
CC = gcc
TARGET = libudpc.so
LIB_OBJECTS =$(LIB_SOURCES:.c=.o)
LDFLAGS= -L.  $(OPT) -Wextra #-lmcheck #-ftlo  #setrlimit on linux 
LIBS= -ldl -lm -lssl -lcrypto -lpthread

CFLAGS =  -I.. -std=c11 -c $(OPT) -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0  -D_GNU_SOURCE -fdiagnostics-color #-Werror -Wwrite-strings #-DDEBUG

all: $(TARGET) test
$(TARGET): $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(LIBS) --shared -o $@

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) -fPIC $< -o $@ -MMD -MF $@.depends 
depend: h-depend
clean:
	rm $(LIB_OBJECTS) $(TARGET) *.o.depends
-include $(LIB_OBJECTS:.o=.o.depends)

test: $(TARGET)
	$(CC) $(CFLAGS) main.c 
	$(CC) $(LDFLAGS) $(LIBS) main.o -ludpc -o test.exe
