/* tazactive: print _NET_ACTIVE_WINDOW + window name. Temp tool. */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>

int
main(void)
{
	Display *d = XOpenDisplay(NULL);
	Atom net_active, utf8, net_name, actual_type;
	int actual_fmt;
	unsigned long n, extra;
	unsigned char *prop = NULL;
	Window active = None;

	if (!d) {
		fprintf(stderr, "tazactive: no display\n");
		return 1;
	}
	net_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	if (XGetWindowProperty(d, DefaultRootWindow(d), net_active,
	    0, 1, False, XA_WINDOW, &actual_type, &actual_fmt, &n, &extra,
	    &prop) == Success && prop) {
		active = *(Window *) prop;
		XFree(prop);
	}
	printf("active=0x%lx\n", active);
	if (active != None) {
		net_name = XInternAtom(d, "_NET_WM_NAME", False);
		utf8 = XInternAtom(d, "UTF8_STRING", False);
		prop = NULL;
		if (XGetWindowProperty(d, active, net_name, 0, 256, False,
		    utf8, &actual_type, &actual_fmt, &n, &extra,
		    &prop) == Success && prop) {
			printf("name=%s\n", (char *) prop);
			XFree(prop);
		}
	}
	XCloseDisplay(d);
	return 0;
}
