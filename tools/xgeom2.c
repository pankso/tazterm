/* xgeom2: frame geometry of a window by name substring (recreation).
 * Walks up with XQueryTree for screen coords. Temp tool. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char **argv)
{
	Display *d;
	Atom net_clients, net_name, utf8, actual_type;
	int actual_fmt;
	unsigned long n, extra, i;
	unsigned char *list = NULL;
	Window root;

	if (argc != 2) {
		fprintf(stderr, "usage: xgeom2 SUBSTRING\n");
		return 1;
	}
	d = XOpenDisplay(NULL);
	if (!d) {
		fprintf(stderr, "xgeom2: no display\n");
		return 1;
	}
	root = DefaultRootWindow(d);
	net_clients = XInternAtom(d, "_NET_CLIENT_LIST", False);
	net_name = XInternAtom(d, "_NET_WM_NAME", False);
	utf8 = XInternAtom(d, "UTF8_STRING", False);

	if (XGetWindowProperty(d, root, net_clients, 0, 128, False,
	    XA_WINDOW, &actual_type, &actual_fmt, &n, &extra,
	    &list) != Success || !list)
		return 1;
	for (i = 0; i < n; i++) {
		Window w = ((Window *) list)[i];
		unsigned char *name = NULL;
		unsigned long nn, ex;

		if (XGetWindowProperty(d, w, net_name, 0, 256, False,
		    utf8, &actual_type, &actual_fmt, &nn, &ex,
		    &name) != Success || !name)
			continue;
		if (strstr((char *) name, argv[1])) {
			Window frame = w, parent = w, *kids = NULL;
			unsigned nkids = 0;
			Window child;
			int x, y;
			unsigned width, height, bw, depth;

			while (1) {
				Window r;
				if (!XQueryTree(d, parent, &r, &parent,
				    &kids, &nkids))
					break;
				if (kids)
					XFree(kids);
				if (parent == r || parent == None)
					break;
				frame = parent;
			}
			XGetGeometry(d, frame, &root, &x, &y, &width,
			    &height, &bw, &depth);
			XTranslateCoordinates(d, frame, root, 0, 0, &x,
			    &y, &child);
			printf("client=0x%lx frame=0x%lx x=%d y=%d w=%u h=%u\n",
			    w, frame, x, y, width, height);
			XFree(name);
			XFree(list);
			XCloseDisplay(d);
			return 0;
		}
		XFree(name);
	}
	XFree(list);
	XCloseDisplay(d);
	fprintf(stderr, "xgeom2: no match\n");
	return 1;
}
