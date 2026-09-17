CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra

X11LIBS := $(shell pkg-config --libs x11 2>/dev/null || echo -lX11)
XTSTLIBS := -lXtst

all: xlist2 xgeom2 tazraise tazactive clicker2 tazkey svg2png sizeloop

xlist2: xlist2.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS)

xgeom2: xgeom2.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS)

tazraise: tazraise.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS)

tazactive: tazactive.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS)

clicker2: clicker2.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS) $(XTSTLIBS)

tazkey: tazkey.c
	$(CC) $(CFLAGS) -o $@ $< $(X11LIBS) $(XTSTLIBS)

svg2png: svg2png.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --cflags --libs gdk-pixbuf-2.0)

sizeloop: sizeloop.c
	$(CC) $(CFLAGS) -o $@ $< -lncurses -ltinfo

clean:
	rm -f xlist2 xgeom2 tazraise tazactive clicker2 tazkey svg2png sizeloop

.PHONY: all clean
