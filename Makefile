CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2
LDFLAGS =

SRCS = epsfs.c epstool.c
OBJS = $(SRCS:.c=.o)
TARGET = epstool

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c epsfs.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@echo "=== Testing EPS Tool ==="
	./$(TARGET) EnsoniqEPS_650MB.hda info
	@echo ""
	@echo "=== Directory listing ==="
	./$(TARGET) EnsoniqEPS_650MB.hda ls -v
	@echo ""
	@echo "=== Directory tree ==="
	./$(TARGET) EnsoniqEPS_650MB.hda tree
	@echo ""
	@echo "=== FAT entries 0-20 ==="
	./$(TARGET) EnsoniqEPS_650MB.hda fat 0 20
