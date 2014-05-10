# sb - simple browser
# See LICENSE file for copyright and license details

VERSION=0.1

CC=gcc
CFLAGS=-g -Wall $(shell pkg-config --cflags gtk+-2.0 webkit-1.0) -DVERSION=\"${VERSION}\"
LDFLAGS+=$(shell pkg-config --libs gtk+-2.0 webkit-1.0)
INCLUDE=/usr/include
LIB=/usr/lib

SRC=sb.c

all: sb

options:
	@echo sb build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

sb: config.h
	$(CC) $(CFLAGS) $(LDFLAGS) -I $(INCLUDE) -L $(LIB) ${SRC} -o sb

clean:
	@echo cleaning
	@rm -rf sb sb-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p sb-${VERSION}
	@cp -R LICENSE makefile config.h README.md \
			TODO NEW \
			${SRC} sb-${VERSION}
	@tar -cf sb-${VERSION}.tar sb-${VERSION}
	@gzip sb-${VERSION}.tar
	@rm -rf sb-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -r sb ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/sb

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/sb

.PHONY: all options clean install uninstall
