CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra
CFLAGS  += $(shell pkg-config --cflags gtk+-3.0 vte-2.91)
LIBS    := $(shell pkg-config --libs gtk+-3.0 vte-2.91)

PREFIX  ?= /usr
BINDIR  ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share

GETTEXT_PACKAGE ?= tazterm
LOCALEDIR ?= $(PREFIX)/share/locale
CFLAGS  += -DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" -DLOCALEDIR=\"$(LOCALEDIR)\"

OBJS = main.o tazterm-window.o tazterm-term.o tazterm-config.o tazterm-split.o tazterm-ai.o \
       tazterm-blocks.o \
       tazterm-ctl.o tazterm-keys.o

all: tazterm

tazterm: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): $(wildcard *.h)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f tazterm $(OBJS)

install: tazterm
	install -Dm755 tazterm $(DESTDIR)$(BINDIR)/tazterm

.PHONY: all clean install
