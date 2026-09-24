# tests/Makefile — built against ../src objects (make -C ../src first).
CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra
CFLAGS  += -I../src -DGETTEXT_PACKAGE=\"tazterm\" \
	$(shell pkg-config --cflags gtk+-3.0 vte-2.91)
LIBS    := $(shell pkg-config --libs gtk+-3.0 vte-2.91)
OBJS    = ../src/tazterm-term.o ../src/tazterm-config.o \
	../src/tazterm-blocks.o ../src/tazterm-ai.o

all: unit gui

unit: unit.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ unit.c $(OBJS) $(LIBS)

gui: gui.c $(OBJS)
	$(CC) $(CFLAGS) -o $@ gui.c $(OBJS) $(LIBS)

check: all
	sh ./run.sh

clean:
	rm -f unit gui

.PHONY: all check clean
