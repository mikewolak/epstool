CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2
LDFLAGS =

# Core filesystem
SRCS = epsfs.c epstool.c
# EFE format handlers
SRCS += efe_raw.c efe_giebler.c

OBJS = $(SRCS:.c=.o)
TARGET = epstool

# Standalone EFE tool
EFEFILE_SRCS = efefile.c efe_raw.c efe_giebler.c
EFEFILE_OBJS = $(EFEFILE_SRCS:.c=.o)
EFEFILE_TARGET = efefile

HEADERS = epsfs.h efe_raw.h efe_giebler.h

.PHONY: all clean test

all: $(TARGET) $(EFEFILE_TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(EFEFILE_TARGET): $(EFEFILE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(EFEFILE_OBJS) $(TARGET) $(EFEFILE_TARGET)

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
