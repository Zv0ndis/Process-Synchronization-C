# Makefile for IOS Project 2 (Ferry Synchronization)

CC=gcc
CFLAGS=-std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS=-pthread -lrt

# Target executable
TARGET=proj2

# Default rule
all: $(TARGET)

# Rule to build the target
$(TARGET): proj2.c
	$(CC) $(CFLAGS) -o $(TARGET) proj2.c $(LDFLAGS)

# Clean rule
clean:
	rm -f $(TARGET)

# phony targets
.PHONY: all clean