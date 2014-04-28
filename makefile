CC=gcc
CFLAGS=-g -Wall $(shell pkg-config --cflags gtk+-2.0 webkit-1.0)
LDFLAGS+=$(shell pkg-config --libs gtk+-2.0 webkit-1.0)
INCLUDE=/usr/include
LIB=/usr/lib
SOURCES=sb.c
OBJ=sb

all: $(SOURCES) $(OBJ)

$(OBJ): $(SOURCES) config.h
	$(CC) $(CFLAGS) $(LDFLAGS) -I $(INCLUDE) -L $(LIB) $(SOURCES) -o $(OBJ)

clean:
	rm -rf $(OBJ)
