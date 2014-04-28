CC=gcc
CFLAGS=-g -Wall $(shell pkg-config --cflags gtk+-2.0 webkit-1.0)
LDFLAGS+=$(shell pkg-config --libs gtk+-2.0 webkit-1.0)
INCLUDE=/usr/include
LIB=/usr/lib

all: sb

sb: sb.c config.h
	$(CC) $(CFLAGS) $(LDFLAGS) -I $(INCLUDE) -L $(LIB) sb.c -o sb

clean:
	rm -rf sb
