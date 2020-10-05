# Makefile for ledyard.c

CC = gcc
CFLAGS = -Wall -pedantic -std=c11 -ggdb -lpthread
PROG = ledyard
OBJS = $(PROG).o

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: clean

clean:	
	rm -rf $(PROG) .*~ *~ *.o *.dSYM core
