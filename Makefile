#
# When building a package or installing otherwise in the system, make
# sure that the variable PREFIX is defined, e.g. make PREFIX=/usr/local
#
PROGNAME=dump1090

ifndef DUMP1090_VERSION
DUMP1090_VERSION=$(shell git describe --always --tags --match=v*)
endif

ifdef PREFIX
BINDIR=$(PREFIX)/bin
SHAREDIR=$(PREFIX)/share/$(PROGNAME)
EXTRACFLAGS=-DHTMLPATH=\"$(SHAREDIR)\"
endif

CPPFLAGS+=-DMODES_DUMP1090_VERSION=\"$(DUMP1090_VERSION)\"
CFLAGS+=-O3 -g -Wall -Werror -W
LIBS=-lpthread -lm
LIBS_RTL=`pkg-config --libs librtlsdr libusb-1.0`
CC=gcc

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
LIBS+=-lrt
CFLAGS+=-std=c11 -D_DEFAULT_SOURCE
endif
ifeq ($(UNAME), Darwin)
UNAME_R := $(shell uname -r)
ifeq ($(shell expr "$(UNAME_R)" : '1[012345]\.'),3)
CFLAGS+=-std=c11 -DMISSING_GETTIME -DMISSING_NANOSLEEP
COMPAT+=compat/clock_gettime/clock_gettime.o compat/clock_nanosleep/clock_nanosleep.o
else
# Darwin 16 (OS X 10.12) supplies clock_gettime() and clockid_t
CFLAGS+=-std=c11 -DMISSING_NANOSLEEP -DCLOCKID_T
COMPAT+=compat/clock_nanosleep/clock_nanosleep.o
endif
endif

ifeq ($(UNAME), OpenBSD)
CFLAGS+= -DMISSING_NANOSLEEP
COMPAT+= compat/clock_nanosleep/clock_nanosleep.o
endif

all: beast-repeater

%.o: %.c *.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EXTRACFLAGS) -c $< -o $@

clean:
	rm -f *.o compat/clock_gettime/*.o compat/clock_nanosleep/*.o dump1090 view1090 faup1090 cprtests crctests

beast-repeater: beast-repeater.o net_io_ex.o anet.o util.o $(COMPAT)
	$(CC) -g -o $@ $^ $(LDFLAGS)
	strip beast-repeater
