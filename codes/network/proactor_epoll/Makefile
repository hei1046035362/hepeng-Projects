# Makefile
CC = gcc
CFLAGS = -g -O2 -march=native -DNDEBUG -pthread
LIBS = -laio -lpthread -lm

# 性能分析支持
CFLAGS += -pg  # 用于gprof分析

TARGET = proactor
SOURCES = hybrid_proactor.c efficient_hybrid_server.c

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LIBS)

clean:
	rm -f $(TARGET) *.o

.PHONY: clean