CC=gcc
CFLAGS_DEBUG=-g3 -Og -fno-omit-frame-pointer -Wall -Wextra
CFLAGS_RELEASE=-O3
LDFLAGS=-ldl

SRCS=$(wildcard *.c)
OBJS=$(patsubst %.c, %.o, $(SRCS))

all: libevilboombox.so

libevilboombox.so: $(SRCS)
	$(CC) -fPIC -shared $(CFLAGS_DEBUG) $(SRCS) -o $@ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f $(OBJS) libevilboombox.so