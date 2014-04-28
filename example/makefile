CC=gcc
CFLAGS=-g -Wall $(shell pkg-config --cflags gtk+-2.0 webkit-1.0)
LDFLAGS+=$(shell pkg-config --libs gtk+-2.0 webkit-1.0)
INCLUDE=/usr/include
LIB=/usr/lib
SOURCES=main.c
OBJ=main

all: $(SOURCES) $(OBJ)

$(OBJ): $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -I $(INCLUDE) -L $(LIB) $(SOURCES) -o $(OBJ)

clean:
	rm -rf $(OBJ)
