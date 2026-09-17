/* xlist2: EWMH client list with geometry + map state (recreation post-reboot).
 * Output: win=ID map=M x=X y=Y w=W h=H [FLAGS] NAME. Temp tool. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>

int
main(void)
{
	Display *d = XOpenDisplay(NULL);
	Atom net_clients, net_name, utf8, actual_type;
	int actual_fmt;
	unsigned long n, extra, i;
	unsigned char *list = NULL;
	Window root;

	if (!d) {
		fprintf(stderr, "xlist2: no display\n");
		return 1;
	}
	root = DefaultRootWindow(d);
	net_clients = XInternAtom(d, "_NET_CLIENT_LIST", False);
	net_name = XInternAtom(d, "_NET_WM_NAME", False);
	utf8 = XInternAtom(d, "UTF8_STRING", False);

	if (XGetWindowProperty(d, root, net_clients, 0, 128, False,
	    XA_WINDOW, &actual_type, &actual_fmt, &n, &extra,
	    &list) != Success || !list) {
		fprintf(stderr, "xlist2: no client list\n");
		return 1;
	}
	printf("clients=%lu\n", n);
	for (i = 0; i < n; i++) {
		Window w = ((Window *) list)[i];
		XWindowAttributes attr;
		Window child;
		int x, y;
		unsigned width, height, bw, depth;
		unsigned char *name = NULL;
		unsigned long nn, ex;

		if (!XGetWindowAttributes(d, w, &attr))
			continue;
		XGetGeometry(d, w, &root, &x, &y, &width, &height, &bw,
		    &depth);
		XTranslateCoordinates(d, w, root, 0, 0, &x, &y, &child);
		if (XGetWindowProperty(d, w, net_name, 0, 256, False,
		    utf8, &actual_type, &actual_fmt, &nn, &ex,
		    &name) != Success || !name)
			name = (unsigned char *) "?";
		printf("win=0x%lx map=%d x=%d y=%d w=%u h=%u [%s] %s\n",
		    w, attr.map_state, x, y, width, height,
		    attr.map_state == IsViewable ? "" :
		    (attr.map_state == IsUnmapped ? "HID" : "UNMAP"),
		    (char *) name);
		if (name && ((char *) name)[0] != '?')
			XFree(name);
	}
	XFree(list);
	XCloseDisplay(d);
	return 0;
}
