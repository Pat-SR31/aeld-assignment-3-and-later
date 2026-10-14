CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall
LDFLAGS ?=

all: aesdsocket
default: aesdsocket

aesdsocket: aesdsocket.c ; $(CC) $(CFLAGS) -o aesdsocket aesdsocket.c $(LDFLAGS)

clean: ; rm -f aesdsocket *.o

.PHONY: all default clean
