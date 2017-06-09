# Himongo Makefile
# Copyright (C) 2010-2011 Salvatore Sanfilippo <antirez at gmail dot com>
# Copyright (C) 2010-2011 Pieter Noordhuis <pcnoordhuis at gmail dot com>
# This file is released under the BSD license, see the COPYING file

LIBBSON_STATICLIB := libbson/.libs/libbson.a
LIBBSON_INC := libbson/src/bson
OBJ=async.o endianconv.o himongo.o net.o proto.o read.o sds.o utils.o
EXAMPLES=himongo-example himongo-example-libevent himongo-example-libev himongo-example-glib

AR_SCRIPT := /tmp/libhimongo.ar

# TESTS=himongo-test
LIBNAME=libhimongo
PKGCONFNAME=himongo.pc

HIMONGO_MAJOR=$(shell grep HIMONGO_MAJOR himongo.h | awk '{print $$3}')
HIMONGO_MINOR=$(shell grep HIMONGO_MINOR himongo.h | awk '{print $$3}')
HIMONGO_PATCH=$(shell grep HIMONGO_PATCH himongo.h | awk '{print $$3}')
HIMONGO_SONAME=$(shell grep HIMONGO_SONAME himongo.h | awk '{print $$3}')

# Installation related variables and target
PREFIX?=/usr/local
INCLUDE_PATH?=include/himongo
LIBRARY_PATH?=lib
PKGCONF_PATH?=pkgconfig
INSTALL_INCLUDE_PATH= $(DESTDIR)$(PREFIX)/$(INCLUDE_PATH)
INSTALL_LIBRARY_PATH= $(DESTDIR)$(PREFIX)/$(LIBRARY_PATH)
INSTALL_PKGCONF_PATH= $(INSTALL_LIBRARY_PATH)/$(PKGCONF_PATH)

# mongo-server configuration used for testing
MONGO_PORT=56379
MONGO_SERVER=mongo-server
define MONGO_TEST_CONFIG
	daemonize yes
	pidfile /tmp/himongo-test-mongo.pid
	port $(MONGO_PORT)
	bind 127.0.0.1
	unixsocket /tmp/himongo-test-mongo.sock
endef
export MONGO_TEST_CONFIG

# Fallback to gcc when $CC is not in $PATH.
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')
CXX:=$(shell sh -c 'type $(CXX) >/dev/null 2>/dev/null && echo $(CXX) || echo g++')
OPTIMIZATION?=-O3
WARNINGS=-Wall -W -Wstrict-prototypes -Wwrite-strings
DEBUG_FLAGS?= -g -ggdb
CFLAGS += -I$(LIBBSON_INC)
REAL_CFLAGS=$(OPTIMIZATION) -fPIC $(CFLAGS) $(WARNINGS) $(DEBUG_FLAGS) $(ARCH)
REAL_LDFLAGS=$(LDFLAGS) $(ARCH)

DYLIBSUFFIX=so
STLIBSUFFIX=a
DYLIB_MINOR_NAME=$(LIBNAME).$(DYLIBSUFFIX).$(HIMONGO_SONAME)
DYLIB_MAJOR_NAME=$(LIBNAME).$(DYLIBSUFFIX).$(HIMONGO_MAJOR)
DYLIBNAME=$(LIBNAME).$(DYLIBSUFFIX)
DYLIB_MAKE_CMD=$(CC) -shared -Wl,-soname,$(DYLIB_MINOR_NAME) -o $(DYLIBNAME) $(LDFLAGS)
STLIBNAME=$(LIBNAME).$(STLIBSUFFIX)
STLIB_MAKE_CMD=ar rcs $(STLIBNAME)

# Platform-specific overrides
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
ifeq ($(uname_S),SunOS)
  REAL_LDFLAGS+= -ldl -lnsl -lsocket
  DYLIB_MAKE_CMD=$(CC) -G -o $(DYLIBNAME) -h $(DYLIB_MINOR_NAME) $(LDFLAGS)
  INSTALL= cp -r
endif
ifeq ($(uname_S),Darwin)
  DYLIBSUFFIX=dylib
  DYLIB_MINOR_NAME=$(LIBNAME).$(HIMONGO_SONAME).$(DYLIBSUFFIX)
  DYLIB_MAKE_CMD=$(CC) -shared -Wl,-install_name,$(DYLIB_MINOR_NAME) -o $(DYLIBNAME) $(LDFLAGS)
endif

all: $(DYLIBNAME) $(STLIBNAME)
# all: $(DYLIBNAME) $(STLIBNAME) $(PKGCONFNAME)

# Deps (use make dep to generate this)
async.o: async.c fmacros.h async.h himongo.h read.h sds.h net.h dict.c dict.h
dict.o: dict.c fmacros.h dict.h
himongo.o: himongo.c fmacros.h himongo.h read.h sds.h net.h
net.o: net.c fmacros.h net.h himongo.h read.h sds.h
read.o: read.c fmacros.h read.h sds.h
sds.o: sds.c sds.h
test.o: test.c fmacros.h himongo.h read.h sds.h

$(LIBBSON_STATICLIB): libbson/Makefile
	cd libbson && make

libbson/Makefile: libbson/autogen.sh
	cd libbson && ./autogen.sh && ./configure

libbson/autogen.sh:
	git submodule update --init

$(DYLIBNAME): $(LIBBSON_STATICLIB) $(OBJ)
	$(DYLIB_MAKE_CMD) $(OBJ) $(LIBBSON_STATICLIB)


$(STLIBNAME):  $(LIBBSON_STATICLIB) $(OBJ)
	@echo "CREATE $@" > $(AR_SCRIPT)
	@echo "ADDLIB $(LIBBSON_STATICLIB)" >> $(AR_SCRIPT)
	@echo "ADDMOD $(OBJ)" >> $(AR_SCRIPT)
	@echo "SAVE" >> $(AR_SCRIPT)
	@echo "END" >> $(AR_SCRIPT)
	@$(AR) -M < $(AR_SCRIPT)
	@rm -f $(AR_SCRIPT)

dynamic: $(DYLIBNAME)
static: $(STLIBNAME)

# Binaries:
himongo-example-libevent: examples/example-libevent.c adapters/libevent.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< -levent $(STLIBNAME)

himongo-example-libev: examples/example-libev.c adapters/libev.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< -lev $(STLIBNAME)

himongo-example-glib: examples/example-glib.c adapters/glib.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< $(shell pkg-config --cflags --libs glib-2.0) $(STLIBNAME)

himongo-example-ivykis: examples/example-ivykis.c adapters/ivykis.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< -livykis $(STLIBNAME)

himongo-example-macosx: examples/example-macosx.c adapters/macosx.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< -framework CoreFoundation $(STLIBNAME)

ifndef AE_DIR
himongo-example-ae:
	@echo "Please specify AE_DIR (e.g. <mongo repository>/src)"
	@false
else
himongo-example-ae: examples/example-ae.c adapters/ae.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. -I$(AE_DIR) $< $(AE_DIR)/ae.o $(AE_DIR)/zmalloc.o $(AE_DIR)/../deps/jemalloc/lib/libjemalloc.a -pthread $(STLIBNAME)
endif

ifndef LIBUV_DIR
himongo-example-libuv:
	@echo "Please specify LIBUV_DIR (e.g. ../libuv/)"
	@false
else
himongo-example-libuv: examples/example-libuv.c adapters/libuv.h $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. -I$(LIBUV_DIR)/include $< $(LIBUV_DIR)/.libs/libuv.a -lpthread -lrt $(STLIBNAME)
endif

ifeq ($(and $(QT_MOC),$(QT_INCLUDE_DIR),$(QT_LIBRARY_DIR)),)
himongo-example-qt:
	@echo "Please specify QT_MOC, QT_INCLUDE_DIR AND QT_LIBRARY_DIR"
	@false
else
himongo-example-qt: examples/example-qt.cpp adapters/qt.h $(STLIBNAME)
	$(QT_MOC) adapters/qt.h -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore | \
	    $(CXX) -x c++ -o qt-adapter-moc.o -c - $(REAL_CFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore
	$(QT_MOC) examples/example-qt.h -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore | \
	    $(CXX) -x c++ -o qt-example-moc.o -c - $(REAL_CFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore
	$(CXX) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. -I$(QT_INCLUDE_DIR) -I$(QT_INCLUDE_DIR)/QtCore -L$(QT_LIBRARY_DIR) qt-adapter-moc.o qt-example-moc.o $< -pthread $(STLIBNAME) -lQtCore
endif

himongo-example: examples/example.c $(STLIBNAME)
	$(CC) -o examples/$@ $(REAL_CFLAGS) $(REAL_LDFLAGS) -I. $< $(STLIBNAME)

examples: $(EXAMPLES)

himongo-test: test.o $(STLIBNAME)

himongo-%: %.o $(STLIBNAME)
	$(CC) $(REAL_CFLAGS) -o $@ $(REAL_LDFLAGS) $< $(STLIBNAME)

test: himongo-test
	./himongo-test

check: himongo-test
	@echo "$$MONGO_TEST_CONFIG" | $(MONGO_SERVER) -
	$(PRE) ./himongo-test -h 127.0.0.1 -p $(MONGO_PORT) -s /tmp/himongo-test-mongo.sock || \
			( kill `cat /tmp/himongo-test-mongo.pid` && false )
	kill `cat /tmp/himongo-test-mongo.pid`

sync_test: $(STLIBNAME) tests/sync_test.c
	$(CC) -o sync_test $(CFLAGS) tests/sync_test.c $(STLIBNAME) -pthread

async_test: $(STLIBNAME) tests/async_test.c tests/ae.c
	$(CC) -o async_test $(CFLAGS) -Itests tests/async_test.c tests/ae.c $(STLIBNAME) -pthread

.c.o:
	$(CC) -std=c99 -pedantic -c $(REAL_CFLAGS) $<

clean:
	rm -rf $(DYLIBNAME) $(STLIBNAME) $(TESTS) $(PKGCONFNAME) examples/himongo-example* *.o *.gcda *.gcno *.gcov

dep:
	$(CC) -MM *.c

ifeq ($(uname_S),SunOS)
  INSTALL?= cp -r
endif

INSTALL?= cp -a

$(PKGCONFNAME): himongo.h
	@echo "Generating $@ for pkgconfig..."
	@echo prefix=$(PREFIX) > $@
	@echo exec_prefix=\$${prefix} >> $@
	@echo libdir=$(PREFIX)/$(LIBRARY_PATH) >> $@
	@echo includedir=$(PREFIX)/$(INCLUDE_PATH) >> $@
	@echo >> $@
	@echo Name: himongo >> $@
	@echo Description: Minimalistic C client library for Mongo. >> $@
	@echo Version: $(HIMONGO_MAJOR).$(HIMONGO_MINOR).$(HIMONGO_PATCH) >> $@
	@echo Libs: -L\$${libdir} -lhimongo >> $@
	@echo Cflags: -I\$${includedir} -D_FILE_OFFSET_BITS=64 >> $@

install: $(DYLIBNAME) $(STLIBNAME) $(PKGCONFNAME)
	mkdir -p $(INSTALL_INCLUDE_PATH) $(INSTALL_LIBRARY_PATH)
	$(INSTALL) himongo.h async.h read.h sds.h adapters $(INSTALL_INCLUDE_PATH)
	$(INSTALL) $(DYLIBNAME) $(INSTALL_LIBRARY_PATH)/$(DYLIB_MINOR_NAME)
	cd $(INSTALL_LIBRARY_PATH) && ln -sf $(DYLIB_MINOR_NAME) $(DYLIBNAME)
	$(INSTALL) $(STLIBNAME) $(INSTALL_LIBRARY_PATH)
	mkdir -p $(INSTALL_PKGCONF_PATH)
	$(INSTALL) $(PKGCONFNAME) $(INSTALL_PKGCONF_PATH)

32bit:
	@echo ""
	@echo "WARNING: if this fails under Linux you probably need to install libc6-dev-i386"
	@echo ""
	$(MAKE) CFLAGS="-m32" LDFLAGS="-m32"

32bit-vars:
	$(eval CFLAGS=-m32)
	$(eval LDFLAGS=-m32)

gprof:
	$(MAKE) CFLAGS="-pg" LDFLAGS="-pg"

gcov:
	$(MAKE) CFLAGS="-fprofile-arcs -ftest-coverage" LDFLAGS="-fprofile-arcs"

coverage: gcov
	make check
	mkdir -p tmp/lcov
	lcov -d . -c -o tmp/lcov/himongo.info
	genhtml --legend -o tmp/lcov/report tmp/lcov/himongo.info

noopt:
	$(MAKE) OPTIMIZATION=""

.PHONY: all test check clean dep install 32bit 32bit-vars gprof gcov noopt
