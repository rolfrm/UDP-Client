OPT = -g3 -O0
SOURCES = udpc.c main.c ../iron/mem.c ../iron/array.c ../iron/log.c ../iron/math.c
CC = gcc
TARGET = run.exe
OBJECTS =$(SOURCES:.c=.o)
LDFLAGS= -L.  $(OPT) -Wextra #-lmcheck #-ftlo  #setrlimit on linux 
LIBS= -ldl -lm -luv

CFLAGS =  -I.. -std=c11 -c $(OPT) -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0  -D_GNU_SOURCE -fdiagnostics-color #-Werror -Wwrite-strings #-DDEBUG

all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LIBS) -ldl -o $@

.c.o: $(HEADERS)
	$(CC) $(CFLAGS) $< -o $@ -MMD -MF $@.depends
depend: h-depend
clean:
	rm $(OBJECTS) $(TARGET) *.o.depends
-include $(OBJECTS:.o=.o.depends)
