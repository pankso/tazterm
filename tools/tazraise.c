/* tazraise: raise+focus a window by name substring via EWMH.
 * Usage: tazraise SUBSTRING. Temp tool. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	Display *d;
	Atom net_clients, net_active, net_name, utf8, actual_type;
	int actual_fmt;
	unsigned long n, extra, i;
	unsigned char *list = NULL;
	Window *wins;
	Window root;

	if (argc != 2) {
		fprintf(stderr, "usage: tazraise SUBSTRING\n");
		return 1;
	}
	d = XOpenDisplay(NULL);
	if (!d) {
		fprintf(stderr, "tazraise: no display\n");
		return 1;
	}
	root = DefaultRootWindow(d);
	net_clients = XInternAtom(d, "_NET_CLIENT_LIST", False);
	net_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	net_name = XInternAtom(d, "_NET_WM_NAME", False);
	utf8 = XInternAtom(d, "UTF8_STRING", False);

	if (XGetWindowProperty(d, root, net_clients, 0, 64, False,
	    XA_WINDOW, &actual_type, &actual_fmt, &n, &extra,
	    &list) != Success || !list) {
		fprintf(stderr, "tazraise: no client list\n");
		return 1;
	}
	wins = (Window *) list;
	for (i = 0; i < n; i++) {
		unsigned char *name = NULL;
		unsigned long nn, ex;

		if (XGetWindowProperty(d, wins[i], net_name, 0, 256,
		    False, utf8, &actual_type, &actual_fmt, &nn, &ex,
		    &name) == Success && name) {
			if (strstr((char *) name, argv[1])) {
				XEvent ev;

				memset(&ev, 0, sizeof(ev));
				ev.xclient.type = ClientMessage;
				ev.xclient.window = wins[i];
				ev.xclient.message_type = net_active;
				ev.xclient.format = 32;
				ev.xclient.data.l[0] = 1;
				ev.xclient.data.l[1] = CurrentTime;
				XSendEvent(d, root, False,
				    SubstructureRedirectMask |
				    SubstructureNotifyMask,
				    &ev);
				XFlush(d);
				printf("raised 0x%lx %s\n", wins[i],
				    (char *) name);
				XFree(name);
				XFree(list);
				XCloseDisplay(d);
				return 0;
			}
			XFree(name);
		}
	}
	XFree(list);
	XCloseDisplay(d);
	fprintf(stderr, "tazraise: no match for '%s'\n", argv[1]);
	return 1;
}
