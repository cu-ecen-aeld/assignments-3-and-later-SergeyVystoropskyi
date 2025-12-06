CC      = $(CROSS_COMPILE)gcc
CFLAGS  = -Wall -Wextra -g
TARGET  = writer
OBJS    = writer.o
 

all: $(TARGET)
 
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^
 
writer.o: writer.c
	$(CC) $(CFLAGS) -c $<
 

clean:
	rm -f *.o
 
.PHONY: all clean
