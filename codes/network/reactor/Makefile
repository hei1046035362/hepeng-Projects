CC = gcc
CFLAGS = -Wall -Wextra -O3 -march=native -pthread
LIBS = -lpthread -latomic

SRCS = ring_queue.c reactor.c server.c
OBJS = $(SRCS:.c=.o)
TARGET = reactor_server

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	sudo ./$(TARGET)